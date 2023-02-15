#!/bin/bash

cd "${0%/*}"
cd src
python pkg_into_header.py
