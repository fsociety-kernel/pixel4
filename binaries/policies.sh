#!/system/bin/sh

chmod 755 /data/local/tmp/magiskpolicy

# allow kernel to execute shell scripts and copy files to fuse partitions
echo "1" > /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel toolbox_exec file { execute }" > /data/local/tmp/policy.log
#/system/bin/sleep 0.1

echo "2" >> /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel shell_exec file { execute }" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

echo "3" >> /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel toolbox_exec file { open }" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

echo "4" >> /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel shell_exec file { open }" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

echo "5" >> /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel toolbox_exec file { execute_no_trans }" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

echo "6" >> /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel shell_exec file { execute_no_trans }" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

echo "7" >> /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel toolbox_exec file { map }" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

echo "8" >> /data/local/tmp/policy.log
/data/local/tmp/magiskpolicy --live "allow kernel shell_exec file { map }" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1
echo "9" >> /data/local/tmp/policy.log

/data/local/tmp/magiskpolicy --live "allow kernel toolbox_exec file { getattr }" >> /data/local/tmp/policy.log
echo "a" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel shell_exec file { getattr }" >> /data/local/tmp/policy.log
echo "b" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel shell_data_file dir { search }" >> /data/local/tmp/policy.log
echo "c" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel mnt_user_file dir { search }" >> /data/local/tmp/policy.log
echo "d" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel kernel capability { dac_read_search }" >> /data/local/tmp/policy.log
echo "e" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel kernel capability { dac_override }" >> /data/local/tmp/policy.log
echo "f" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel fuse dir { search }" >> /data/local/tmp/policy.log
echo "g" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel fuse file { getattr }" >> /data/local/tmp/policy.log
echo "h" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1

/data/local/tmp/magiskpolicy --live "allow kernel fuse file { open }" >> /data/local/tmp/policy.log
echo "i" >> /data/local/tmp/policy.log
#/system/bin/sleep 0.1
