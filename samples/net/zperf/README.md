# zperf

This sample enables the ``zperf`` library, enabling upload
throughput testing with an ``iperf`` server.

Upload throughput testing is triggered through the ``zperf_upload``
RPC.

## UDP Upload Test

On a PC, start the ``iperf`` UDP server (``--interval 1`` outputs throughput information each second):
```
iperf-2.2.1-win64.exe --server --interval 1 --udp
```
Run the ``zperf_upload`` RPC on the device to test (local gateway in this example)
```
infuse rpc --gateway zperf_upload --udp --address "192.168.20.78" --duration 2000 --rate 1000
```

## TCP Upload Test

On a PC, start the ``iperf`` UDP server (``--interval 1`` outputs throughput information each second):
```
iperf-2.2.1-win64.exe --server --interval 1
```
Run the ``zperf_upload`` RPC on the device to test (local gateway in this example)
```
infuse rpc --gateway zperf_upload --tcp --address "192.168.20.78" --duration 2000 --rate 1000
```
