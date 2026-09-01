# 📋 Auditoría de Requisitos y Lista de Issues — Webserv (42)

Este documento detalla todas las discrepancias, errores de lógica y requisitos pendientes encontrados en el código de **Webserv** en comparación directa con el **Subject oficial de 42**.

---

## 📑 Resumen de Severidad

| Severidad | Cantidad | Impacto |
| :--- | :---: | :--- |
| 🔴 **CRÍTICO** | 4 | Motivo directo de **Nota 0** o fallo total en evaluación / tester. |
| 🟠 **ALTO** | 2 | Incumplimiento explícito del Subject o bug funcional notable. |
| 🟡 **MEDIO** | 3 | Mala práctica contra las reglas de 42 o fallo de configuración. |
| 🔵 **BAJO** | 1 | Mejora de robustez y seguridad (Edge Cases). |

---

## 🔴 1. Issues Críticos (Prioridad Máxima)

### [ISSUE-01] Bloqueo en ejecución de CGI y E/S de tuberías (`pipes`) fuera de `poll()`
* **Archivos afectados:** `sources/CGIHandler.cpp` (método `forkExecute`).
* **Requisito del Subject:**
  > *"I/O that can wait for data (sockets, pipes/FIFOs, etc.) must be non-blocking and driven by a single poll() (or equivalent). Calling read/recv or write/send on these descriptors without prior readiness will result in a grade of 0."*
  > *"A request to your server should never hang indefinitely."*
* **Problema detectado:**
  1. El proceso padre realiza un `write()` bloqueante en `pipeToChild[1]` para enviar el cuerpo de la petición. Si el cuerpo supera el buffer de la tubería del kernel (~64 KB), el servidor entra en **deadlock**.
  2. El proceso padre ejecuta un bucle `while (read(pipeFromChild[0], ...))` bloqueante y un `waitpid(pid, &status, 0)` sincrónico.
  3. Si un script CGI se queda en bucle infinito (`while True:`) o tarda en responder, **todo el servidor web se congela para todos los clientes**.
* **Solución requerida:**
  - Configurar las tuberías con `O_NONBLOCK`.
  - Integrar los descriptores de lectura de las tuberías CGI en el bucle principal de `poll()`, o gestionar la espera con `waitpid(pid, &status, WNOHANG)` y un temporizador para matar el proceso hijo con `kill(pid, SIGKILL)` y devolver `504 Gateway Timeout` tras unos segundos.

---

### [ISSUE-02] Rutas absolutas hardcodeadas en `ResponseBuilder.cpp`
* **Archivos afectados:** `sources/ResponseBuilder.cpp` (líneas 520 y 600).
* **Problema detectado:**
  ```cpp
  // Líneas 520 (POST) y 600 (DELETE):
  if (uploadPath.substr(0, std::string("/home/jrey-roj/juanma/webserv/servDir/upload/").length()).find("/home/jrey-roj/juanma/webserv/servDir/upload/"))
  {
      return (ErrorHandler::generateHttpResponse(403, server));
  }
  ```
  - La ruta del usuario original (`/home/jrey-roj/...`) está grabada a fuego en el código.
  - En cualquier otra máquina o usuario (incluido el evaluador), **todas las operaciones `POST` (subida de archivos) y `DELETE` fallan inmediatamente con `403 Forbidden`**.
* **Solución requerida:**
  - Eliminar esa comprobación hardcodeada. Validar la ruta contra `location.upload_store` o `location.root` de manera dinámica.

---

### [ISSUE-03] Falta de soporte para `Transfer-Encoding: chunked`
* **Archivos afectados:** `sources/RequestParser.cpp` (líneas 122–128).
* **Requisito del Subject:**
  > *"Just remember that, for chunked requests, your server needs to un-chunk them, the CGI will expect EOF as the end of the body."*
* **Problema detectado:**
  ```cpp
  if (this->request.method == "POST" && !this->request.headers.count("Content-Length"))
  {
      this->phase = COMPLETE;
      this->request.error = 400; // Rechaza peticiones chunked sin Content-Length
      return;
  }
  ```
  - Si un cliente envía un `POST` con fragmentación HTTP (`Transfer-Encoding: chunked`), el servidor devuelve error `400 Bad Request` en lugar de desencapsular el payload.
* **Solución requerida:**
  - Añadir soporte en `RequestParser` para detectar la cabecera `Transfer-Encoding: chunked`, leer los tamaños de bloque en hexadecimal (`<hex_size>\r\n<data>\r\n`) y reconstruir el cuerpo hasta recibir el bloque final `0\r\n\r\n`.

---

### [ISSUE-04] Modificación insegura del vector `pollfds` durante la iteración (Vector Invalidation)
* **Archivos afectados:** `sources/ServerManager.cpp` (métodos `run`, `acceptNewClient`, `closeClient`).
* **Problema detectado:**
  - Dentro del bucle `for (size_t i = 0; i < this->pollfds.size(); i++)`:
    - Al aceptar clientes se invoca `this->pollfds.push_back(fds)`. Si el vector supera su capacidad, se reasigna la memoria interna, invalidando punteros subyacentes.
    - Al cerrar clientes se invoca `this->pollfds.erase(begin() + i)`. Esto reduce el tamaño del vector y desplaza los elementos a la izquierda, provocando que el índice `i++` **se salte el siguiente descriptor de la lista** en esa ronda de `poll()`.
* **Solución requerida:**
  - Decrementar el índice (`i--`) tras un `erase()` o acumular los descriptores a agregar/eliminar en listas temporales y aplicarlos al finalizar la iteración de la ronda actual.

---

## 🟠 2. Issues de Severidad Alta

### [ISSUE-05] Falta de `chdir()` en el proceso hijo antes de ejecutar CGI
* **Archivos afectados:** `sources/CGIHandler.cpp` (método `forkExecute`).
* **Requisito del Subject:**
  > *"The CGI should be run in the correct directory for relative path file access."*
* **Problema detectado:**
  - El proceso hijo ejecuta `execve()` manteniendo el directorio de trabajo del servidor (`CWD`), en lugar de cambiar al directorio donde se encuentra el script CGI. Si el script utiliza rutas relativas para leer/escribir archivos locales, fallará.
* **Solución requerida:**
  - Obtener el directorio del script (ej. `/servDir/var/www/cgi-bin/`) y ejecutar `chdir(scriptDir.c_str())` en el hijo antes de `execve()`.

---

### [ISSUE-06] Parseo defectuoso de Cabeceras HTTP con espacios y bug en `Host`
* **Archivos afectados:** `sources/RequestParser.cpp` (método `parseLine`).
* **Problemas detectados:**
  1. **Truncamiento de cabeceras:** Se usa `splitBySpaces(line)` para separar clave y valor. Cualquier cabecera con espacios en el valor (como `User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)`) se descompone en más de 3 tokens y se trunca, guardando únicamente `Mozilla/5.0`.
  2. **Bug de condición en C++ para `Host`:**
     ```cpp
     if (this->request.headers.find("Host")->second.find(":")) // ❌
     ```
     En C++, `find()` devuelve `std::string::npos` (`~0ULL`) si no encuentra `:`. Al ser distinto de cero, la condición evalúa a **`true`** aun cuando no existan dos puntos.
* **Solución requerida:**
  - Extraer cabeceras buscando la primera aparición del carácter `:` con `line.find(':')`, tomando la izquierda como clave y la derecha (con trim) como valor completo.
  - Corregir a: `if (host.find(':') != std::string::npos)`.

---

## 🟡 3. Issues de Severidad Media

### [ISSUE-07] Comprobación de `errno` tras operaciones `recv()` y `send()`
* **Archivos afectados:** `sources/ConnectionHandler.cpp` (líneas 98 y 227).
* **Requisito del Subject:**
  > *"Checking the value of errno to adjust the server behaviour is strictly forbidden after performing a read or write operation."*
* **Problema detectado:**
  - Se comprueba `if (errno == EAGAIN || errno == EWOULDBLOCK)` tras llamar a `recv()` y `send()`.
* **Solución requerida:**
  - En un modelo no bloqueante gobernado por `poll()`, si `poll()` dio listo:
    - `recv() == 0` $\rightarrow$ Desconexión del cliente (cerrar conexión).
    - `recv() < 0` $\rightarrow$ Error de lectura (cerrar conexión).
    - `send() <= 0` $\rightarrow$ Error de envío (cerrar conexión).
  - No basar el flujo de control en la variable global `errno`.

---

### [ISSUE-08] Archivo de configuración por defecto vacío
* **Archivos afectados:** `servDir/config/default.config` y `configfile/defaultConfig.conf`.
* **Problema detectado:**
  - Si el usuario ejecuta `./webserv` sin argumentos, el servidor intenta abrir `servDir/config/default.config`, que tiene 0 bytes, provocando una excepción y la salida inmediata del programa.
* **Solución requerida:**
  - Añadir un bloque `server { ... }` completo y funcional en `default.config`.

---

### [ISSUE-09] Rutas absolutas ajenas en los archivos `.config` de ejemplo
* **Archivos afectados:** `servDir/config/serverConfig.config`, `servDir/config/config1.config`.
* **Problema detectado:**
  - Las directivas `root`, `cgi_path` y `upload_store` apuntan a rutas de `/home/jrey-roj/...`.
* **Solución requerida:**
  - Actualizar los archivos `.config` para que utilicen rutas relativas o acordes a la máquina actual.

---

## 🔵 4. Issues de Severidad Baja (Robustez)

### [ISSUE-10] Comprobación parcial de Path Traversal (`..`)
* **Archivos afectados:** `sources/RequestParser.cpp` (líneas 87–93).
* **Problema detectado:**
  - Solo se comprueba `uri.size() > 1 && uri[1] == '.' && uri[2] == '.'`.
  - Peticiones del tipo `/images/../../../../etc/passwd` no quedan neutralizadas por esta validación.
* **Solución requerida:**
  - Comprobar de forma general si la URI contiene `"/../"` o termina en `"/.."`.

---

## 🛠️ Plan de Trabajo Sugerido para Resolver los Issues

1. **Paso 1 (Inmediato):** Eliminar las líneas hardcodeadas de `ResponseBuilder.cpp` y corregir los `.config`.
2. **Paso 2 (Seguridad CGI):** Añadir `chdir()` en el hijo y control con `WNOHANG` / timeout en `CGIHandler.cpp`.
3. **Paso 3 (Robustez de Red):** Corregir la mutación del vector `pollfds` en `ServerManager.cpp` y eliminar dependencias de `errno`.
4. **Paso 4 (Parsing HTTP):** Refactorizar el extractor de cabeceras y añadir soporte básico para `chunked` en `RequestParser.cpp`.
5. **Paso 5 (Testing):** Probar con el tester oficial de 42, `siege -b` y pruebas manuales con `curl`.
