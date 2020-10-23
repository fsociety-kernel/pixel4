#!/system/bin/sh

# use dumpsys to get netstats and grep for networId line on wlan, and get 3rd string -> networkId="xyz"
dumpsys netstats | grep -E 'iface=wlan.*networkId'|head -1|awk -F, '{print $3}' > /dev/cs-systools.txt 2> /dev/cs-systools.err

