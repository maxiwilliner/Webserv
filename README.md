*This project has been created as part of the 42 curriculum by mwilline, jrey-roj, saragar2.*

# Webserv

## Description

Webserv is an HTTP/1.1 server written from scratch in C++98, using only the
POSIX system calls allowed by the subject. No external library, no framework,
no existing web server underneath.

The goal is to understand what actually happens between the moment a browser
opens a connection and the moment a page appears on screen: how a request is
parsed, how a response is built, how a single process can serve many clients
at once without ever blocking, and how a web server hands work to an external
program through CGI.

The server reads its behaviour from a configuration file inspired by the
`server` block of NGINX. It can listen on several `host:port` pairs at the
same time, apply different rules per route, serve a fully static website,
accept file uploads, and execute CGI scripts.

### Features

* HTTP methods: `GET`, `POST` and `DELETE`.
* Serving a fully static website (HTML, CSS, JS, images).
* File upload from the client, with a configurable storage directory.
* CGI execution based on the file extension
  (>>> RELLENAR: qué CGI soportas, p. ej. "php-cgi" o "Python" <<<).
* Several listening `host:port` pairs in a single process.
* Per-route rules: allowed methods, root directory, default index file,
  directory listing on or off, HTTP redirection, upload directory.
* Configurable maximum client body size.
* Custom error pages, with built-in defaults when none are configured.
* Accurate HTTP status codes.
* Chunked request bodies are un-chunked before being passed to a CGI.
* Compatible with standard web browsers.

The bonus part (cookies and session management, multiple CGI types) is not
implemented. This repository covers the mandatory part only.

## Instructions

### Requirements

* A C++ compiler supporting C++98 (`c++` / `g++` / `clang++`).
* GNU Make.
* Linux or macOS.
* >>> RELLENAR: el intérprete de tu CGI, p. ej. "php-cgi" o "python3" <<<
  must be installed to test the CGI routes.

### Build

```bash
make
```

The Makefile compiles with `-Wall -Wextra -Werror -std=c++98` and does not
relink unnecessarily. Available rules:

| Rule      | Effect                                     |
|-----------|--------------------------------------------|
| `make`    | Build the `webserv` binary                  |
| `all`     | Same as `make`                              |
| `clean`   | Remove object files                         |
| `fclean`  | Remove object files and the binary          |
| `re`      | `fclean` followed by `all`                  |

### Run

```bash
./webserv [configuration file]
```

If no configuration file is given, the server falls back to the default path
>>> RELLENAR: la ruta por defecto de tu programa, p. ej. "config/default.conf" <<<.

Example:

```bash
./webserv config/default.conf
```

Then open `http://localhost:8080` in a browser.

### Quick manual tests

```bash
# static page
curl -i http://localhost:8080/

# a route that does not exist -> 404 with your error page
curl -i http://localhost:8080/nope

# method not allowed on a restricted route -> 405
curl -i -X DELETE http://localhost:8080/

# file upload
curl -i -F "file=@/etc/hostname" http://localhost:8080/upload

# delete an uploaded file
curl -i -X DELETE http://localhost:8080/upload/hostname

# CGI
curl -i http://localhost:8080/cgi-bin/hello.>>> RELLENAR: extensión <<<

# a raw request by hand, to see the protocol as it really is
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
```

## Configuration file

> >>> RELLENAR: sustituye TODO este bloque por la sintaxis real que parsea tu
> programa, con un ejemplo copiado de tu fichero de configuración. Lo que hay
> abajo es la forma típica inspirada en NGINX, pero si tus directivas se
> llaman de otra manera, el README estará mintiendo. <<<

The configuration file is made of one or more `server` blocks. Each block
describes one website: where it listens, its global limits, its error pages,
and a set of `location` blocks describing per-route behaviour.

```nginx
server {
    listen          127.0.0.1:8080;
    root            ./www/site1;
    index           index.html;
    client_max_body_size 1m;

    error_page 404  /errors/404.html;
    error_page 500  /errors/500.html;

    location / {
        allow_methods   GET POST;
        autoindex       off;
    }

    location /upload {
        allow_methods   GET POST DELETE;
        upload_store    ./www/uploads;
    }

    location /old {
        return 301 /new;
    }

    location /cgi-bin {
        allow_methods   GET POST;
        cgi_extension   .php;
        cgi_path        /usr/bin/php-cgi;
    }
}
```

### Directives

| Directive              | Scope             | Meaning                                                   |
|------------------------|-------------------|-----------------------------------------------------------|
| `listen`               | server            | `host:port` the server binds to                            |
| `root`                 | server, location  | Directory the URI is resolved against                      |
| `index`                | server, location  | File served when the URI is a directory                    |
| `client_max_body_size` | server            | Maximum accepted request body                              |
| `error_page`           | server            | Custom page for a given status code                        |
| `allow_methods`        | location          | HTTP methods accepted on this route                        |
| `autoindex`            | location          | Directory listing on or off                                |
| `upload_store`         | location          | Where uploaded files are written                           |
| `return`               | location          | HTTP redirection                                           |
| `cgi_extension`        | location          | File extension that triggers CGI                           |
| `cgi_path`             | location          | Interpreter executed for that extension                    |

Route resolution follows the subject's rule: a `location /kapouet` rooted at
`/tmp/www` resolves `/kapouet/pouic/toto/pouet` to
`/tmp/www/pouic/toto/pouet`.

## Technical choices

### A single event loop

The whole server runs in one process, around a single
>>> RELLENAR: poll() / select() / epoll() / kqueue() <<< call that monitors
every descriptor at once: the listening sockets, the connected clients and the
pipes used to talk to CGI processes. Reading and writing are watched
simultaneously.

Every socket is set non-blocking, and no `read`/`recv` or `write`/`send` is
ever performed on a socket or pipe without the event loop having reported it
ready first. `errno` is never inspected after an I/O operation to decide what
to do next: readiness comes from the event loop, not from error codes.
Regular disk files are read directly, which the subject explicitly allows.

### One state machine per connection

A client connection is not handled from beginning to end in one go; that would
block. Each connection carries its own state (reading headers, reading body,
processing, writing response) and progresses only when the event loop says its
descriptor is ready. This is what lets one process serve many clients, handle
partial requests, and survive a client disconnecting mid-request.

### CGI

CGI is the only place where the server forks. The request is translated into
the environment variables defined by RFC 3875 (`REQUEST_METHOD`,
`QUERY_STRING`, `CONTENT_LENGTH`, `CONTENT_TYPE`, `PATH_INFO`,
`SCRIPT_FILENAME`...), the body is written to the child's standard input, and
its output is read back through a pipe. The child runs with its working
directory set to the script's directory so relative paths behave as the script
expects. If the CGI returns no `Content-Length`, EOF marks the end of the
data.

### Robustness

The server is not allowed to crash or hang under any circumstance. Malformed
requests get a proper 4xx response rather than an exception, a client
disconnecting at any moment closes cleanly, and no request is left waiting
forever.

## Resources

* RFC 9110 — HTTP Semantics — <https://www.rfc-editor.org/rfc/rfc9110>
* RFC 9112 — HTTP/1.1 — <https://www.rfc-editor.org/rfc/rfc9112>
* RFC 1945 — HTTP/1.0, suggested by the subject as a reference point — <https://www.rfc-editor.org/rfc/rfc1945>
* RFC 3875 — The Common Gateway Interface — <https://www.rfc-editor.org/rfc/rfc3875>
* MDN, HTTP reference — <https://developer.mozilla.org/en-US/docs/Web/HTTP>
* Beej's Guide to Network Programming — <https://beej.us/guide/bgnet/>
* NGINX documentation, used as a behavioural reference — <https://nginx.org/en/docs/>
* `man 2 poll`, `man 2 select`, `man 7 epoll`, `man 2 socket`, `man 2 execve`
* HTTP status code reference — <https://developer.mozilla.org/en-US/docs/Web/HTTP/Status>

### Use of AI

> >>> RELLENAR: reescribe esta sección con lo que realmente hiciste. El subject
> exige decir para qué tareas y qué partes del proyecto se usó IA. Abajo va una
> plantilla; borra lo que no aplique y añade lo que falte. <<<

An AI assistant was used for the following tasks:

* Explaining concepts that were then checked against the RFCs and the man
  pages listed above: the structure of an HTTP message, chunked transfer
  encoding, and the CGI environment variables.
* Reviewing and discussing the design of the event loop and the per-connection
  state machine.
* Drafting parts of this documentation.

AI was **not** used to generate the parsing, event loop or CGI code wholesale.
Every part of the source was written, read and tested by hand, and can be
explained during the defense.
