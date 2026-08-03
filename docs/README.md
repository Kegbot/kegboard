# Kegboard Docs

Sphinx sources for the Kegboard manual, published at
<https://docs.kegbot.org/projects/kegboard> as a subproject of the main
Kegbot docs site.

The toolchain is managed by [uv](https://docs.astral.sh/uv/); any make
target syncs it from `uv.lock` automatically:

```console
$ make html          # output in _build/html/
$ make livehtml      # live-rebuild server while editing
```
