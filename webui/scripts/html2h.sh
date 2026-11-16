#!/usr/bin/env bash
# 3X4X: "./html2h.sh" makes webui_html.h (Spanish); "./html2h.sh en" makes webui_html_en.h from webui_en.html.
# The array keeps the same name in every language: the firmware includes only one of them.
cd ..
SUF=""
[ -n "$1" ] && [ "$1" != "es" ] && SUF="_$1"
gzip -c9 build/webui${SUF}.html > build/webui_html && xxd -i build/webui_html > build/webui_html${SUF}.h && rm build/webui_html && sed -i -e 's/unsigned char build_webui_html\[]/const uint8_t webui_html[] PROGMEM/' build/webui_html${SUF}.h && sed -i -e 's/unsigned int build_webui_html_len/const unsigned int webui_html_len/' build/webui_html${SUF}.h
rm -fv build/webui_html${SUF}.h-e

echo "webui_html${SUF}.h GENERATED in webui/build/";
