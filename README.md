# lpd-modoki

---
## Description

An lpd-alike application to get printer image data.

**Not intended to be used with public or IPv6-based network.**


## Usage

```
# lpd-modoki
lpd-modoki -a [ip address] -p [portnum] -q [queue] -f[filename]
#
```

As default, listen at 0.0.0.0 (INADDR_ANY), port 515, any queue accepted and use stdout.

### Example

```
# lpd-modoki | gpcl6 -dNOPAUSE -sDEVICE=pdfimage24 -sOutputFile=output.pdf -
```

## Limitation

- no support IPv6
- no support multiple queue
- no support large image file
- no support daemonize

No plan to fix them.

## License

WTFPL (http://www.wtfpl.net/)
