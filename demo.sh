#!/bin/bash

cd "${0%/*}"

make clean
make

cd example_heat_equation
make clean
make

echo ""
echo ""
echo -e "\e[1;37mOpen a browser to \e[1;36mhttp://localhost:9736/test.html\e[1;37m for demo\e[0m"
(trap 'kill 0' SIGINT; python -m http.server 9736 &>/dev/null & ./a.out)
