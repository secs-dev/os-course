# Perfetto Usage Docs

Download tracebox:
```sh
curl -LO get.perfetto.dev/tracebox
chmod +x ./tracebox
```

 Run tracing deamon:
```sh
sudo ./tracebox traced
```

```sh
sudo ./tracebox traced_probes
```

Run tracing
```sh
sudo ./tracebox perfetto -c /mnt/utm-share/util/perfetto/config.txtpb --txt -o /tmp/trace.pftrace
# copy trace from internal storage to shared folder
sudo cp /tmp/trace.pftrace /mnt/utm-share/util/perfetto/
```

Open URL in browser:

- https://ui.perfetto.dev/

And open trace in web-application (Drag-and-Drop is supported)

Navigation through trace is "WASD" (see SUPPORT > Keyboard Shortcuts)
