# Tracing the linux kernel

## How to trace a given kernel-space function and visualize the CFG using python

### Terms

- ftrace: A powerful tracing framework within the Linux kernel used to monitor and analyze the execution of kernel functions. It provides detailed information about function calls, including entry and exit points, which can be used to trace the execution flow in real-time.

- CFG (Control Flow Graph): A graphical representation of all paths that might be traversed through a program during its execution. It is used to visualize the flow of control between different blocks of code, making it easier to understand the structure and behavior of the program.

### Tracing the kernel

Enter the directory `lab` and execute `make all`. Copy the directory `lab` into the QEMU
filesystem and produce the trace log of ftrace. See `lab/README.md` for more information.

### Visualize the output CFG

Use the script `graph_ftrace.py` to produce visual grpahs from the output of ftrace.
You may need to modify the `main()` a little (to pick your logs to parse).
