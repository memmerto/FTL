#!/bin/sh

BASEDIR=/tmp/pihole

# Kill possibly running pihole-FTL process
while pidof -s pihole-FTL > /dev/null; do
  pid="$(pidof -s pihole-FTL)"
  echo "Terminating running pihole-FTL process with PID ${pid}"
  kill $pid
  sleep 1
done

# Clean up possible old files from earlier test runs
rm -f $BASEDIR/gravity.db $BASEDIR/pihole-FTL.db $BASEDIR/pihole.log $BASEDIR/pihole-FTL.log /dev/shm/FTL-*

# Create necessary directories and files
mkdir -p $BASEDIR
touch $BASEDIR/pihole-FTL.log $BASEDIR/pihole.log $BASEDIR/pihole-FTL.pid $BASEDIR/pihole-FTL.port

# Copy binary into a location the new user pihole can access
cp ./pihole-FTL $BASEDIR/
chmod +x $BASEDIR/pihole-FTL
# Note: We cannot add CAP_NET_RAW and CAP_NET_ADMIN at this point
setcap CAP_NET_BIND_SERVICE+eip $BASEDIR/pihole-FTL

# Prepare gravity database
sqlite3 $BASEDIR/pihole/gravity.db < test/gravity.db.sql

# Prepare pihole-FTL database
sqlite3 $BASEDIR/pihole/pihole-FTL.db < test/pihole-FTL.db.sql

# Prepare setupVars.conf
cat >$BASEDIR/setupVars.conf <<EOF
BLOCKING_ENABLED=true
EOF

# Prepare pihole-FTL.conf
cat >$BASEDIR/pihole-FTL.conf <<EOF
DEBUG_ALL=true
RESOLVE_IPV4=no
RESOLVE_IPV6=no
LOGFILE=$BASEDIR/pihole-FTL.log
PIDFILE=$BASEDIR/pihole-FTL.pid
EOF

# Prepare dnsmasq.conf
cat >$BASEDIR/dnsmasq.conf <<EOF
log-queries
log-facility=$BASEDIR/pihole.log
EOF

# Set restrictive umask
OLDUMASK=$(umask)
umask 0022

# Prepare LUA scripts
mkdir -p /opt/pihole/libs
wget -O /opt/pihole/libs/inspect.lua https://ftl.pi-hole.net/libraries/inspect.lua

# Terminate running FTL instance (if any)
if pidof pihole-FTL &> /dev/null; then
  echo "Terminating running pihole-FTL instance"
  killall pihole-FTL
  sleep 2
fi

# Start FTL
cd $BASEDIR
if ! ./pihole-FTL; then
  echo "pihole-FTL failed to start"
  exit 1
fi

# Prepare BATS
mkdir -p test/libs
git clone --depth=1 --quiet https://github.com/bats-core/bats-core test/libs/bats > /dev/null

# Block until FTL is ready, retry once per second for 45 seconds
sleep 2

# Print versions of pihole-FTL
echo -n "FTL version: "
drill +TXT +CHAOS version.FTL @127.0.0.1 +short
echo -n "Contained dnsmasq version: "
drill +TXT +CHAOS version.bind @127.0.0.1 +short

# Print content of pihole.log and pihole-FTL.log
cat $BASEDIR/pihole.log
cat $BASEDIR/pihole-FTL.log

# Run tests
test/libs/bats/bin/bats "test/test_suite.bats"
RET=$?

if [[ $RET != 0 ]]; then
  openssl s_client -quiet -connect tricorder.pi-hole.net:9998 2> /dev/null < /var/log/pihole-FTL.log
fi

# Kill pihole-FTL after having completed tests
pkill pihole-FTL

# Restore umask
umask $OLDUMASK

# Remove copied file
rm $BASEDIR/pihole-FTL

# Exit with return code of bats tests
exit $RET
