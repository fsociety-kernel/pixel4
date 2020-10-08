#!/system/bin/sh

/system/bin/dmesg > /data/local/tmp/uci-cs-dmesg.txt

cp /data/local/tmp/uci-cs-dmesg.txt /storage/emulated/0/__uci-cs-dmesg.txt
cp /sys/fs/pstore/console-ramoops-0 /storage/emulated/0/__console-ramoops-0.txt

