#!/bin/sh
case "$1" in
  pre)  /usr/bin/python3 /usr/local/bin/pcstate.py off ;;
  post) /usr/bin/python3 /usr/local/bin/pcstate.py on & ;;
esac
