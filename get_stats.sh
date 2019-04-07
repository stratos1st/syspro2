#!/bin/bash

users_exited=0
files_send=0
files_rcv=0
dirs_send=0
dirs_rcv=0
max_usr=-1
min_usr=99999 #not completly correct
total_rcved=0
total_send=0
i=0

while read LINE; do
   if [[ "$LINE" == "rcv "* ]]; then
     #echo "he recved $LINE"
     tmp=$(echo "$LINE" | tr -dc '0-9')
     total_rcved=$(( $total_rcved + $tmp ))
   elif [[ "$LINE" == "send "* ]]; then
     #echo "he sended $LINE"
     tmp=$(echo "$LINE" | tr -dc '0-9')
     total_send=$(( $total_send + $tmp ))
   elif [[ "$LINE" == "exiting" ]]; then
     #echo "exited"
     users_exited=$(( $users_exited + 1 ))
   elif [[ "$LINE" == "sended file" ]]; then
     #echo "sended"
     files_send=$(( $files_send + 1 ))
   elif [[ "$LINE" == "rcved file" ]]; then
     #echo "rcved"
     files_rcv=$(( $files_rcv + 1 ))
   elif [[ "$LINE" == "sended dir" ]]; then
     #echo "rcved"
     dirs_send=$(( $dirs_send + 1 ))
   elif [[ "$LINE" == "rcved dir" ]]; then
     #echo "rcved"
     dirs_rcv=$(( $dirs_rcv + 1 ))
   else
     #echo "user $LINE"
     users_loged[$i]=$(echo "$LINE" | tr -dc '0-9')
     if [[ max_usr -lt ${users_loged[i]} ]]; then
       max_usr=${users_loged[i]}
     fi
     if [[ min_usr -gt ${users_loged[i]} ]]; then
       min_usr=${users_loged[i]}
     fi
     i=$(( $i + 1 ))
   fi
done

echo "users entered= $i"
echo "list:"
echo "${users_loged[*]}"
echo "users exited= $users_exited"
echo "min user id= $min_usr"
echo "max user id= $max_usr"
echo "total files sended= $files_send"
echo "total files recived= $files_rcv"
echo "total dirs sended= $dirs_send"
echo "total dirs recived= $dirs_rcv"
echo "total bites sended= $total_send"
echo "total bites recived= $total_rcved"
