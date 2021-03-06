
Necronda web server
===================

## Features

* Full IPv4 and IPv6 support
* Serving local files via HTTP and HTTPS
  * File compression ([gzip](https://www.gzip.org/), [Brotli](https://www.brotli.org/)) and disk cache for compressed files
* Reverse proxy for other HTTP and HTTPS servers
* FastCGI support (e.g. [PHP-FPM](https://php-fpm.org/))
* Support for [MaxMind's GeoIP Database](https://www.maxmind.com/en/geoip2-services-and-databases)
* Optional DNS reverse lookup for connecting hosts
* Automatic URL rewrite (e.g. `/index.html` -> `/`, `/test.php` -> `/test`)
* Modern looking and responsive error documents


## Configuration

See [docs/example.conf](docs/example.conf) for more details.


### Global directives

* `certificate` - path to SSL certificate (or certificate chain)
* `private_key` - path to SSL private key
* `geoip_dir` (optional) - path to a directory containing GeoIP databases
* `dns_server` (optional) - address of a DNS server


### Virtual host configuration

* `[<host>]` - begins section for the virtual host `<host>`
* Local
    * `webroot` - path to the root directory
    * `dir_mode` - specify the behaviour for directories without an `index.html` or `index.php`
        * `forbidden` - the server will respond with `403 Forbidden`
        * `info` - try passing *path info* to an upper `.php` file.
        * `list` - list contents of directory (**not implemented yet**)
* Reverse proxy
    * `hostname` - hostname of server to be reverse proxy of
    * `port` - port to be used
    * `http` - use HTTP to communicate with server
    * `https` - use HTTPS to communicate with server
