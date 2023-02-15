#!/bin/bash

cd "${0%/*}"

cd example_heat_equation
make clean
make

printf "\n\n\e[1;37mOpen a browser to \e[1;36mhttp://localhost:9736/heat_equation.html\e[1;37m for demo\e[0m\n"
(trap 'kill 0' SIGINT; python -m http.server 9736 &>/dev/null & ./a.out)
