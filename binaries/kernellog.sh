#!/system/bin/sh

/system/bin/dmesg > /dev/uci-cs-dmesg.txt
chcon u:object_r:shell_data_file:s0 /dev/uci-cs-dmesg.txt
#cat /dev/uci-cs-dmesg.txt > /storage/emulated/0/__uci-cs-dmesg.txt
touch /storage/emulated/0/__uci-cs-dmesg.txt

cd /sys/fs/pstore/
cat console-ramoops-0

cat /sys/fs/pstore/console-ramoops-0 > /dev/console-ramoops-0.txt
chcon u:object_r:shell_data_file:s0 /dev/console-ramoops-0.txt
#cat /dev/console-ramoops-0.txt > /storage/emulated/0/__console-ramoops-0.txt
touch /storage/emulated/0/__console-ramoops-0.txt



