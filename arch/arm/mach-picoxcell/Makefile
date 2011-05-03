obj-y				:= picoxcell_core.o io.o axi2cfg.o \
				   time.o \
				   devices.o \
				   clk.o \
				   mux.o \
				   device_armgpio.o \
				   gpio.o \
				   bus_err.o \
				   gpioirq.o \
				   device_uart.o \
				   device_spacc.o \
				   device_trng.o \
				   device_fuse.o \
				   device_emac.o
obj-$(CONFIG_BOARD_PC7302)	+= board_pc7302.o
obj-$(CONFIG_PICOXCELL_PC3X2)	+= pc3x2.o
obj-$(CONFIG_PICOXCELL_PC3X3)	+= pc3x3.o
obj-$(CONFIG_PICOXCELL_PC30XX)	+= pc30xx.o
obj-$(CONFIG_CPU_FREQ)		+= cpufreq.o
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PICOXCELL_HAVE_TSU)+= tsu.o
obj-$(CONFIG_PICOXCELL_UICC)	+= device_uicc.o
