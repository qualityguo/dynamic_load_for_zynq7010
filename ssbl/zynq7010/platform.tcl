# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct D:\RTOS_Study\ssbl\ssbl\zynq7010\platform.tcl
# 
# OR launch xsct and run below command.
# source D:\RTOS_Study\ssbl\ssbl\zynq7010\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {zynq7010}\
-hw {D:\RTOS_Study\ssbl\vivado\zynq7010.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -out {D:/RTOS_Study/ssbl/ssbl}

platform write
platform generate -domains 
platform active {zynq7010}
domain active {zynq_fsbl}
bsp reload
bsp reload
domain active {standalone_domain}
bsp reload
bsp setlib -name xilffs -ver 4.5
bsp setlib -name xilrsa -ver 1.6
bsp write
bsp reload
catch {bsp regenerate}
bsp setlib -name xilskey -ver 7.1
bsp setlib -name xilflash -ver 4.9
bsp write
bsp reload
catch {bsp regenerate}
platform generate
