# SSN-1: Smart Sensor Node

A temperature monitoring program for industrial use, developed as a school project.

## Overview

SSN-1 simulates a smart sensor that reads ambient temperature every second for one minute, calculates the average, and sends the data to a remote server via TCP/HTTP. The system logs measurements in a rolling 24-hour buffer and supports configurable temperature threshold warnings.

## Features

- **Temperature monitoring**: Simulated sensor readings every 1 second
- **Data averaging**: Calculates average over 60 readings (1 minute)
- **Local logging**: Circular buffer storing 24 hours of averaged data
- **Remote transmission**: Sends data to server using TCP/HTTP POST requests
- **Threshold alerts**: Configurable low/high temperature warnings
- **Non-blocking I/O**: Asynchronous network operations

## Usage
```bash
./ssn-1 <low_threshold> <high_threshold>
```
This sets the warning thresholds to 15°C (low) and 25°C (high). The program will continuously monitor temperature and alert when readings fall outside this range.

## License

MIT.

## Author

**Student**: Tim Ackevald
**School**: Chas Academy, SUVX25