# include (perf_monitor.pri)

PLUGIN_ID=perf_monitor
PLUGIN_NAME=Performance monitor
PLUGIN_VENDOR=Unipro
PLUGIN_MODE=ui
unix_not_mac() : LIBS += -lproc
include( ../../ugene_plugin_common.pri )
