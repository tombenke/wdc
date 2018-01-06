# wdc
Weather Data Collector

Run the NATS server in docker:

```bash
    docker run -it --rm --network=host -p 4222:4222 -p 6222:6222 -p 8222:8222 --name nats-main nats
```
