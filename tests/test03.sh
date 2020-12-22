#!/bin/sh
#
# Demonstrates the use of specified socket files
echo
echo "************************************ TEST 3 ************************************"

echo
echo "*** Starting server, listening on .testpipe..."
./bin/server -f .testpipe &
SERVERPID=$!
echo "*** Starting server, listening on .pipe2..."
./bin/server -f .pipe2 &
SERVERPID2=$!
sleep 1

echo
echo "*** Starting client, connecting to .testpipe..."
./bin/client -f .testpipe -u asdf -c "submit 10 123123123 10 sleep 20"
./bin/client -f .testpipe -u asdf -c "list"
echo
echo "*** Starting client, connecting to .pipe2..."
./bin/client -f .pipe2 -u asdf -c "submit 12 321321321 12 df -h"
./bin/client -f .pipe2 -u asdf -c "list"

echo
echo "*** Shutting down server..."
/bin/kill -INT $SERVERPID
/bin/kill -INT $SERVERPID2
