#!/bin/sh
cat hosts |grep -v '#'|grep "0">__hosts_k
zip -9 hosts_k.zip __hosts_k