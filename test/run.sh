#!/bin/sh

BASEDIR=/tmp/pihole

# Clean up possible old files from earlier test runs
rm -f $BASEDIR/gravity.db $BASEDIR/pihole-FTL.db $BASEDIR/pihole.log $BASEDIR/pihole-FTL.log

# Create necessary directories and files
mkdir -p $BASEDIR
touch $BASEDIR/pihole-FTL.log $BASEDIR/pihole.log $BASEDIR/pihole-FTL.pid $BASEDIR/pihole-FTL.port

# Prepare gravity database
sqlite3 $BASEDIR/gravity.db < test/gravity.db.sql

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

cp pihole-FTL $BASEDIR/

# Set restrictive umask
OLDUMASK=$(umask)
umask 0022

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

# Kill pihole-FTL after having completed tests
pkill pihole-FTL

# Restore umask
umask $OLDUMASK

# Remove copied file
rm $BASEDIR/pihole-FTL

# Exit with return code of bats tests
exit $RET
