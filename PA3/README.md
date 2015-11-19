# Web proxy
This contains the code for a small server that proxies web requests

# Overview
This program will fork a new instance everytime a client connects to it. It will establish a socket connection and parse requests until it has received a complete HTTP request. It will send the HTTP request to the requested server and it will continually forward chunks from the server to the client until either party closes the connection. For this reason, it is very inefficient and will perform web requests very slowly.

# Commands
make -To make executables

make clean  - To clean executables

Start with: ./webproxy [port]

