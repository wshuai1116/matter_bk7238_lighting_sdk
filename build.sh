#!/bin/bash
opt=$1
project=$2
fw_version=$3
sub_version=$4
example=$5
soc_name=bk7231n

if [ $# -lt 4 ] || [ "${opt}" != "clean" ] && [ "${opt}" != "new" ] && [ "${opt}" != "r" ]; then 
   echo "usage: ./build.sh (clean/r/new) projec(FC41D/FCM100D/FCM242D/FGM842D/F02D) fw_version sub_version example(WINDOW/LIGHT/LOCK/THERMOSTAT)"
   exit
fi

case ${project} in
    FC41D)
       soc_name=bk7231n
    ;;
    FCM100D)
       soc_name=bk7231n
    ;;
    FCM242D)
       soc_name=bk7238
    ;;
    FGM842D)
       soc_name=bk7238
    ;;
    F02D)
       soc_name=bk7238
    ;;
    *)
        echo "usage: ./build.sh (clean/r/new) (FC41D/FCM100D/FCM242D/FGM842D/F02D) fw_version sub_version example(WINDOW/LIGHT/LOCK/THERMOSTAT)"  
        exit 
    ;;
esac

if [ ! -d ./toolchain/gcc-arm-none-eabi-10.3-2021.10 ];then 
    tar -xvf ./toolchain/gcc-arm-none-eabi-10.3-2021.10.tar.bz2 -C ./toolchain
fi

export FREERTOS_EXEC_PATH=$(pwd)/toolchain/gcc-arm-none-eabi-10.3-2021.10/bin/
export QUEC_PROJECT=${project}
export PLAT=${soc_name}
export PATH="$(pwd)/toolchain:$PATH"

if [ "${example}" = "WINDOW" ];
then
   export MATTER_EXAMPLE=window-covering-app
elif [ "${example}" = "LIGHT" ];
then
   export MATTER_EXAMPLE=lighting-app
elif [ "${example}" = "LOCK" ];
then
   export MATTER_EXAMPLE=lock-app
elif [ "${example}" = "THERMOSTAT" ];
then
   export MATTER_EXAMPLE=thermostat
elif [ "${example}" = "PLUG" ];
then
   export MATTER_EXAMPLE=on-off-plug
else
   echo "usage: ./build.sh (clean/r/new) (FC41D/FCM100D/FCM242D/FGM842D/F02D) fw_version sub_version example(WINDOW/LIGHT/LOCK/THERMOSTAT)"  
   exit
fi

if [ "${opt}" = "clean" ];
then
   make clean
   exit
elif [ "${opt}" = "r" ];
then
   make ${soc_name} -j8
elif [ "${opt}" = "new" ];
then
   make clean
   rm -rf ${top_dir}/out
   make ${soc_name} -j8

else
   echo "usage: ./build.sh (clean/r/new) (FC41D/FCM100D/FCM242D/FGM842D/F02D) fw_version sub_version example(WINDOW/LIGHT/LOCK/THERMOSTAT)"  
   exit
fi

