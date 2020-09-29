#!/bin/sh
cat hosts |grep -v '#'|grep "0">hosts_k
zip -9 hosts_k.zip hosts_k