#!/bin/sh
#
# Demonstrates enabling/disabling runtime debugging
echo
echo "************************************ TEST 4 ************************************"

echo
echo "*** Starting server (debug off)..."
rm -f .cse376hw4.socket
./bin/server &
SERVERPID=$!
sleep 1

echo
echo "*** Client connecting and submitting job (muted)..."
./bin/client -u asdf -c "submit 10 123123123 12 ls" 1>/dev/null 2>/dev/null &
sleep 1

echo
echo "*** Enabling debug mode on server..."
/bin/kill -USR1 $SERVERPID
sleep 1

echo
echo "*** Client connecting and submitting job (muted)..."
./bin/client -u asdf -c "list" 1>/dev/null 2>/dev/null &
sleep 1

echo
echo "*** Shutting down server..."
/bin/kill -INT $SERVERPID
