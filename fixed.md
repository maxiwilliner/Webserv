# Registro de Fixes y Mejoras Realizadas en Webserv (fixed.md)

Este documento detalla todas las correcciones, mejoras arquitectónicas y soluciones a fallos críticos implementadas en el servidor **Webserv (42 School)**.

---

## Resumen Ejecutivo de Cambios

| ID | Categoría | Componente | Descripción de la Solución | Estado |
|---|---|---|---|---|
| **ISSUE-01** | **CGI Asíncrono** | `ServerManager`, `CGIHandler`, `ConnectionHandler` | Desacoplamiento de pipes de lectura CGI para monitorizarlas directamente en el único `poll()` central. Estado `WAITING_CGI` no bloqueante con timeout de 5 segundos (`504 Gateway Timeout`). | **Solucionado** |
| **ISSUE-02** | **Rutas Hardcodeadas** | `ResponseBuilder` | Eliminación total de rutas fijas `/home/jrey-roj/...` en métodos POST y DELETE. Validación dinámica con `location.upload_store` y `location.root`. | **Solucionado** |
| **ISSUE-03** | **Chunked Body** | `RequestParser` | Implementación de máquina de estados para decodificar `Transfer-Encoding: chunked` (tamaños en hexadecimal y bloques hasta chunk final `0\r\n\r\n`). | **Solucionado** |
| **ISSUE-04** | **Mutación de `pollfds`** | `ServerManager` | Eliminación de inserciones/eliminaciones en caliente durante el bucle de iteración. Uso de vectores diferidos `fds_to_add` y `fds_to_close`. | **Solucionado** |
| **ISSUE-05** | **CGI Directory** | `CGIHandler` | Invocación de `chdir(scriptDir)` en el proceso hijo antes de `execve()` usando la ruta relativa del script (`./script.py`). | **Solucionado** |
| **ISSUE-06** | **Parsing de Cabeceras** | `RequestParser` | Búsqueda por delimitador `:` y `Utils::ft_trim` conservando espacios dentro de valores (ej: `User-Agent: Mozilla/5.0...`) y corrección del puerto en `Host`. | **Solucionado** |
| **ISSUE-07** | **Eliminación de `errno`** | `ConnectionHandler` | Supresión de cualquier condicional `errno == EAGAIN || errno == EWOULDBLOCK` tras operaciones de `recv()` y `send()`. | **Solucionado** |
| **ISSUE-08** | **Configuración por Defecto** | `default.config`, `ConfigFileParser` | Creación de `servDir/config/default.config` funcional y soporte para rutas relativas y absolutas en `ConfigFileParser`. | **Solucionado** |
| **EXTRA** | **Utilidades y Errores** | `Utils`, `ErrorHandler` | Implementación de funciones faltantes (`ft_trim`, `statusText`, `getFileExtension`, `urlDecode`) y soporte para códigos `502` y `504`. | **Solucionado** |

---

## 1. ISSUE-01 & ISSUE-05: CGI Totalmente Asíncrono y No Bloqueante con `chdir`

### Problema Anterior
- `CGIHandler::forkExecute` creaba las tuberías y ejecutaba un bucle bloqueante `while (read(...))` y `waitpid(pid, &status, 0)` dentro del hilo principal del servidor.
- Durante la ejecución de cualquier script CGI (o en caso de bucle infinito/sleep en el script), el servidor completo se congelaba, violando la regla del subject: *"It must be non-blocking and use only 1 poll() for all I/O operations"*.
- El hijo no cambiaba de directorio de trabajo antes de `execve()`.

### Solución Implementada
1. **Separación de Responsabilidades en `CGIHandler`**:
   - `CGIHandler::startCGI(...)`: Configura variables de entorno (`REQUEST_METHOD`, `SCRIPT_NAME`, `SCRIPT_FILENAME`, `QUERY_STRING`, `CONTENT_LENGTH`, `CONTENT_TYPE`, `SERVER_PROTOCOL`, `GATEWAY_INTERFACE`, `HTTP_HOST`, `HTTP_USER_AGENT`), crea pipes con `O_NONBLOCK` en el extremo de lectura, realiza `fork()`, en el hijo hace `chdir(scriptDir)` y ejecuta `execve()`. En el padre escribe el body en `pipeToChild[1]`, lo cierra (enviando EOF) y devuelve inmediatamente el descriptor `readPipeFd` y el `pid`.
   - `CGIHandler::parseOutput(...)`: Función estática que toma el buffer acumulado del script CGI, procesa las cabeceras HTTP (`Status`, `Content-Type`, `Location`, `Content-Length`) y separa el cuerpo de la respuesta.
2. **Integración en `poll()` de `ServerManager`**:
   - Se añadió `std::map<int, int> pipeToClientMap` para mapear `pipe_fd -> client_fd`.
   - El descriptor `readPipeFd` se añade a la lista de `pollfds` con evento `POLLIN`.
   - El socket del cliente pasa al estado `ConnectionHandler::WAITING_CGI` y se desactivan temporalmente sus eventos de socket mientras espera la salida del CGI.
   - En cada vuelta del bucle `poll()`, cuando `readPipeFd` está listo (`POLLIN`), se lee un bloque no bloqueante con `read()`. Al alcanzar EOF (`read() == 0`), se cierra el pipe, se obtiene el estado del hijo mediante `waitpid(pid, &status, WNOHANG)`, se procesa la salida con `CGIHandler::parseOutput`, se asigna `client->response`, se pasa el estado a `WRITING` y se activa `POLLOUT` en el socket del cliente.
3. **Control de Timeout Asíncrono (504 Gateway Timeout)**:
   - `ServerManager::checkCGITimeouts()` comprueba en cada ciclo si un cliente en estado `WAITING_CGI` supera los 5 segundos (`now - cgiStartTime > 5`).
   - Si expira el tiempo, se envía `kill(pid, SIGKILL)`, se recoge con `waitpid(WNOHANG)`, se cierra el descriptor del pipe, se genera automáticamente una respuesta `504 Gateway Timeout` y se pasa a `WRITING`, sin haber bloqueado en ningún momento la atención de otros clientes.

---

## 2. ISSUE-02: Eliminación de Rutas Hardcodeadas

### Problema Anterior
- `sources/ResponseBuilder.cpp` (antiguas líneas 520 y 600) contenía comprobaciones fijas con cadenas `/home/jrey-roj/juanma/webserv/servDir/upload/`. Cualquier petición en otro entorno o máquina fallaba sistemáticamente con error 403.

### Solución Implementada
- Se eliminaron completamente las comparaciones con rutas hardcodeadas.
- La ruta destino para subidas y borrados se resuelve dinámicamente:
  - Si la localización tiene configurada la directiva `upload_store`, se utiliza dicho directorio base.
  - En caso contrario, se utiliza `location.root`.
  - Se extrae el nombre de archivo a partir de la URI relativa a `location.locationPath` (o se genera un nombre automático si la URI apunta al directorio raíz de subida).

---

## 3. ISSUE-03: Soporte Completo de `Transfer-Encoding: chunked`

### Problema Anterior
- `RequestParser.cpp` asumía que todas las peticiones con cuerpo incluían una cabecera `Content-Length`. Si un cliente enviaba peticiones con `Transfer-Encoding: chunked`, la petición era rechazada con código 400.

### Solución Implementada
- Se añadió soporte de decodificación chunked en `include/RequestParser.hpp` y `sources/RequestParser.cpp`.
- Se implementó una máquina de estados:
  - `CHUNK_SIZE_PHASE`: Lee la línea que contiene el tamaño del bloque en formato hexadecimal (`std::strtol(..., 16)`). Si el tamaño es `0`, pasa a la fase de trailers/finalización.
  - `CHUNK_DATA_PHASE`: Acumula los bytes de datos exactos correspondientes al bloque indicado y descarta el delimitador CRLF de separación.
  - `CHUNK_TRAILER_PHASE`: Al recibir la línea vacía final tras el chunk `0`, marca la petición como `COMPLETE` y calcula el `Content-Length` total acumulado.

---

## 4. ISSUE-04: Mutación Segura de Descriptores en `pollfds`

### Problema Anterior
- Durante la iteración `for (size_t i = 0; i < this->pollfds.size(); i++)`, las llamadas a `acceptNewClient` y `closeClient` insertaban (`push_back`) o eliminaban (`erase`) elementos directamente sobre `this->pollfds`, provocando invalidación de iteradores e índices.

### Solución Implementada
- Se implementó un esquema de actualización diferida:
  - Se declaran dos listas locales en cada vuelta del bucle: `std::vector<pollfd> fds_to_add` y `std::vector<int> fds_to_close`.
  - Durante el procesamiento de eventos, los nuevos sockets y pipes se registran en `fds_to_add`, y los descriptores a cerrar se encolan en `fds_to_close`.
  - Al concluir la iteración de eventos, se ejecutan en orden todos los cierres y posteriormente se agregan los nuevos descriptores.

---

## 5. ISSUE-06: Parseo Robusto de Cabeceras y Corrección de `Host`

### Problema Anterior
- `RequestParser.cpp` dividía las líneas de cabecera por espacios (`splitBySpaces`), truncando valores de cabeceras que contienen espacios legítimos (como `User-Agent: Mozilla/5.0 ...`).
- La validación `if (this->request.headers.find("Host")->second.find(":"))` contenía un bug conceptual porque `std::string::find` devuelve `0` (que evalúa a `false`) si los dos puntos están al inicio, o `std::string::npos` (que evalúa a `true`) si no se encuentran.

### Solución Implementada
- Búsqueda directa del primer carácter `:` con `line.find(':')`.
- Extracción limpia de la clave y el valor utilizando `Utils::ft_trim` para eliminar espacios iniciales y finales.
- Comprobación correcta de puerto en `Host`:
  ```cpp
  size_t colonHost = this->request.headers["Host"].find(':');
  if (colonHost != std::string::npos)
  {
      this->request.headers["Host"] = this->request.headers["Host"].substr(0, colonHost);
  }
  ```

---

## 6. ISSUE-07: Eliminación de Comprobaciones de `errno` en E/S de Red

### Problema Anterior
- El subject prohíbe explícitamente: *"Checking the value of errno to adjust the server behaviour is strictly forbidden after performing a read or write operation"*.
- `ConnectionHandler.cpp` comprobaba `errno == EAGAIN || errno == EWOULDBLOCK` tras `recv()` y `send()`.

### Solución Implementada
- En modo no bloqueante controlado por `poll()`:
  - En `readRequest()`: si `recv()` devuelve `<= 0`, se retorna inmediatamente `false` (desconexión limpia del cliente o error), sin consultar `errno`. Si devuelve `> 0`, los datos se añaden al buffer interno y se procesa la petición.
  - En `writeResponse()`: si `send()` devuelve `<= 0`, se retorna `false`. Si devuelve `> 0`, se actualiza `sendOffset` y al completar el buffer se transiciona al estado `READY`.

---

## 7. ISSUE-08: Configuración por Defecto y Rutas en `ConfigFileParser`

### Problema Anterior
- `servDir/config/default.config` estaba vacío.
- `ConfigFileParser.cpp` requería estrictamente que toda directiva `root` empezara por `/`, impidiendo el uso de rutas relativas al proyecto.

### Solución Implementada
- Se pobló `servDir/config/default.config` con una configuración estándar lista para usar:
  - Escucha en puerto `8080` (nombre `localhost`).
  - Rutas raíz e índices configurados.
  - Soporte de páginas de error `404` y `500`.
  - Localizaciones para `/`, `/cgi-bin/`, `/upload/` (con `upload_enable on; autoindex on;`) y `/autoindex/`.
- Se ajustó `ConfigFileParser.cpp` para validar tanto rutas relativas válidas como absolutas, impidiendo rutas maliciosas con `..`.

---

## 8. Verificación y Resultados de Tests

Se ejecutó la suite de pruebas completa en el entorno WSL (Ubuntu 24.04) compilando con las flags de 42:
`c++ -Wall -Wextra -Werror -std=c++98 -fsanitize=address -g`

### Resultados de la Batería de Pruebas:
1. **GET `/`**: Código `200 OK`, entrega correcta del archivo `index.html`.
2. **GET `/autoindex/`**: Código `200 OK`, generación dinámica del listado de directorio.
3. **POST `/upload/test.txt`**: Código `201 Created`, archivo creado en disco.
4. **GET `/upload/test.txt`**: Código `200 OK`, contenido verificado: `"Hello 42 Webserv"`.
5. **POST Chunked `/upload/chunked.txt`**: Código `201 Created`, cuerpo decodificado correctamente.
6. **GET `/upload/chunked.txt`**: Código `200 OK`, contenido verificado: `"Chunked message body payload"`.
7. **DELETE `/upload/test.txt`**: Código `204 No Content`, archivo eliminado del sistema.
8. **GET `/upload/test.txt` posterior a DELETE**: Código `404 Not Found`.
9. **CGI GET (`/cgi-bin/getrequest.py?user=42student`)**: Código `200 OK`, salida procesada con query string.
10. **CGI POST (`/cgi-bin/echo.py`)**: Código `200 OK`, cuerpo de petición recibido y reflejado por el script.
11. **CGI Timeout (`/cgi-bin/sleep.py`)**: Código `504 Gateway Timeout` tras 5.23 segundos, proceso hijo eliminado de forma limpia sin bloquear el hilo del servidor.
