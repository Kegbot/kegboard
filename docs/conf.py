# Sphinx configuration for the Kegboard manual, published at
# https://docs.kegbot.org/projects/kegboard as a subproject of the
# main Kegbot docs site (github.com/Kegbot/kegbot-overview-docs).

project = "Kegboard"
copyright = "2003-2026, The Kegbot Project Contributors"
author = "The Kegbot Project Contributors"
release = "4.0.0-pre1"

extensions = [
    "myst_parser",
]
myst_enable_extensions = [
    "deflist",
    "smartquotes",
    "replacements",
]
# The protocol specs deep-link their own numbered subsections.
myst_heading_anchors = 4

# The protocol specs use "..." ellipses inside JSON examples, which the
# strict JSON lexer rejects before Pygments retries in relaxed mode.
suppress_warnings = ["misc.highlighting_failure"]

# README.md documents how to build this manual; it is not part of it.
exclude_patterns = ["_build", "README.md", "Thumbs.db", ".DS_Store"]

html_theme = "furo"
html_theme_options = {
    "light_logo": "kegbot-logo-black.png",
    "dark_logo": "kegbot-logo-white.png",
}
html_static_path = ["_static"]
html_title = "Kegboard"
