#!/system/bin/sh

/system/bin/dmesg > /data/local/tmp/uci-cs-dmesg.txt
chcon u:object_r:shell_data_file:s0 /data/local/tmp/uci-cs-dmesg.txt
touch /storage/emulated/0/__uci-cs-dmesg.txt

cd /sys/fs/pstore/
cat console-ramoops-0

cat /sys/fs/pstore/console-ramoops-0 > /data/local/tmp/console-ramoops-0.txt
chcon u:object_r:shell_data_file:s0 /data/local/tmp/console-ramoops-0.txt
touch /storage/emulated/0/__console-ramoops-0.txt



