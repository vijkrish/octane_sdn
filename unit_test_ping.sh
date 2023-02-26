!/bin/bash
subnet="10.5.51"
host=3
while [ $host -lt 255 ]
do
    ping -c 1 "$subnet.$host" &
    host=$((host+1))
done
