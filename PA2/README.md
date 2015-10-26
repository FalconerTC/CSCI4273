# Distributed File Storage
This contains the server and client code for running a distributed file system

# Directory Structure
client      - Holds client source and config

server      - Holds server source and config

DFS         - Primary directory for server-side file storage

upload      - Primary directory for client-side file storage (defined in client config)

retrieval   - Storage for files retrieved from server

# Commands
make -To make executables

make clean  - To clean executables

make scrub - To clean and clear server storage and retrieval

run ./run to do basic start (make, start four servers and get client start command)
