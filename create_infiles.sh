#!/bin/bash

# if we are too unlucky random names can be generated twice!

#check number of params
if [ $# -ne 4 ] ; then
  echo "wrong number of arguments"
  exit 1
fi

dir_name=$1
num_of_files=$2
num_of_dirs=$3
levels=$4

#check if params are intiger
intiger='^[1-9]+'
if ! [[ ( $num_of_files =~ $intiger ) && ( $num_of_dirs =~ $intiger ) && ( $levels =~ $intiger ) ]] ; then
   echo "error: parameter not a number"
   exit 1
fi

echo "1 arg: $1"
echo "2 arg: $2"
echo "3 arg: $3"
echo "4 arg: $4"

#make directory
if [ ! -d "$dir_name" ] ; then
  mkdir $dir_name
fi

#find script directory
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "${script_dir}/${dir_name}" #go to dir_mane to start making files and dirs

#make files in "home" directory
#how many files will i make
how_many_files=$(( $num_of_files / ($num_of_dirs + 1) + (1 <= ($num_of_files % ($num_of_dirs + 1))) ))
#echo "  ${dir_name} ${how_many_files}"
for j in $(seq 1 $how_many_files) ; do
  #random length and file name
  len8=$(( $RANDOM % 7 ))
  len8=$(( $len8 + 1 ))
  random_file_name=$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w $len8 | head -n 1)

  #random length and string
  # 128 kb is 128000 chars
  # 1 kb is 1000 chars
  len128=$(( $RANDOM % (128-100) ))
  len128=$(( $len128 + 100 ))
  random_string=$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w $len128 | head -n 1)

  #make random file
  echo $random_string > $random_file_name
done
#update values for the rest of the files
num_of_files=$(( $num_of_files - $how_many_files ))


lvl=1 #counter for levels
#make directories and files
for i in $(seq 1 $num_of_dirs) ; do

  #random length and directory name
  len8=$(( $RANDOM % 7 ))
  len8=$(( $len8 + 1 ))
  random_dir_name=$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w $len8 | head -n 1)

  #make directory and go there
  mkdir $random_dir_name
  cd $random_dir_name

  #how many files will i make
  how_many_files=$(( $num_of_files / $num_of_dirs + ($i <= ($num_of_files % $num_of_dirs )) ))
  #echo "${i} ${random_dir_name} ${how_many_files}"
  #make files in curr directory
  for j in $(seq 1 $how_many_files) ; do
    #random length and file name
    len8=$(( $RANDOM % 7 ))
    len8=$(( $len8 + 1 ))
    random_file_name=$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w $len8 | head -n 1)

    #random length and string
    # 128 kb is 128000 chars
    # 1 kb is 1000 chars
    len128=$(( $RANDOM % (128-100) ))
    len128=$(( $len128 + 100 ))
    random_string=$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w $len128 | head -n 1)

    #make random file
    echo $random_string > $random_file_name
  done

  #where to make next dir (if we make another level or not)
  if [ $lvl -eq 0 ] ; then
    cd "${script_dir}/${dir_name}"
  fi

  lvl=$(( ($lvl + 1) % $levels ))
done
