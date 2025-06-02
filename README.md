# Internet_Protocols_project2 - CSC 573 
This project was completed with a partner as part of a CSC 573 assignment at NC State University. Permission to upload this project to a public repository was granted by the CSC 573 professor.
## Team Members

#### 1. Hasini Chenchala (hchench)
#### 2. Aiden Lapp (adlapp)

## Instructions to run the code
1. Install Dependencies
```
sudo apt install git-all g++ python3 cmake build-essential libsqlite3-dev python3-dev pkg-config sqlite3
```
2. Download and extract ns-3
```
wget https://www.nsnam.org/releases/ns-allinone-3.41.tar.bz2
sudo apt-get install bzip2
tar xfj ns-allinone-3.41.tar.bz2
cd ns-allinone-3.41/ns-3.41
```
3. Configure and build ns-3
```
./ns3 configure --enable-examples --enable-tests
./ns3 build
```
4. Compile and execute simulation
   
   First move the tcp_hchench_adlapp.cc file to ns-allinone-3.41/ns-3.41/scratch directory. For example if you downloaded/extracted ns-3 and the source code to your home directory, run:
```
mv ~/tcp_hchench_adlapp.cc ~/ns-allinone-3.41/ns-3.41/scratch
```
Then to compile and execute, run:

```
./ns3 run scratch/tcp_hchench_adlapp.cc
```
5. View results
   
   The resulting tcp_hchench_adlapp.csv file will be in the ns-allinone-3.41/ns-3.41 directory.

## Implementation details
- Point-to-Point Link Delay:
   - We utilized a point-to-point delay of .25ms.
   - This low latency would be utilized in a data center environment.
   - This low delay helps improve throughput
- RED Queue Discipline Configuration:
   -  Random Early Detection:
      - This is an Active Queue Management algorithm that detects early signs of congestion and randomly marks or drops packets before the queue overflows.
      - If we did not implement RED, queues would fill up completely before dropping packets, which would lead to increased latency and unfair resource allocation.
      - This helps prevent global synchronization of TCP flows and improves overall network performance.
      - Global synchronization occurs when multiple TCP connections reduce their transmission rates simultaneously in reponse to congestion, then simultaneously increase them, which creates an oscillating network utilization pattern.  
   - Minimum Threshold:
      - We configured the Minimum Threshold to be 20 packets.    
      - The marking congestion begins when the average queue size reaches 20 packets.
      - This allows marking congestion to begin early, which allows for a very responsive congestion control.
   - Maximum Threshold:
      -  We configured the Maximum Threshold to be 60 packets.
      -  All packets will be marked/dropped when the average queue size reaches 60 packets.
      -  This enables more aggressive ECN marking.
   - Queue Weight:
      - We chose a queue weight of 0.0625.
      - Since this is higher than the standard value of 0.002, the queue will be more responsive to queue changes
      - This value is used to help compute an average queue length.
      - A queue weight of 0.0625 means that in the calculation of the average queue size, 6.25% weight will be placed on the current queue size, while the other 93.75% weight will be placed on the previous average.
   - Link Delay:
      - We configured our link delay to be 0.25ms.
      - This matches our point-to-point delay, which ensures consistency between the actual network delay and our RED queue model.
   - Feng Adaptive:
      - We enabled feng adaptive.
      - This helps by dynamically adjusting the marking probabilities based on queue length. 
   - Gentle Mode:
      - We enabled gentle mode to ensure smooth transitions in packet marking probabilities.
- TCP Protocol Configuration:
   - Per-node TCP configuration:
      - This allows us to explicity set the TCP type to Cubic for S1 and the TCP type to DCTCP for S2, in experiment 5 (as opposed to if we used a global configuration).
   - DCTCP parameters:
      - DctcpShiftG:
         - Set to 0.0625.
         - Standard value which controls how quickly DCTCP responds to congestion.  
      - DctcpAlphaOnInit: 
         - Set to 1.0.
         - This sets the initial alpha value for the DCTCP algorithm, representing the fraction of packets that have experienced congestion.
         - We changed our value from 0.0625 (which is a standard conservative value that allows DCTCP to start with a moderate response to congestion) to 1.0 because we were noticing an extremely unfair distribution of resources, and since the file transfer is relatively small, DCTCP was taking too long to recover, so we wanted to immediately begin recovering.
      - UseEct0:
         - We enabled UseEct0, as this is needed for ECN compatibility.      
- Flow Parameters:
   - Flow Duration: 
      - We chose a flow duration of 120 seconds.
      - This value should be long enough to ensure complete data transfer for all experiments.
   - Data Transfer Size:
      - We used a data transfer size of 50MB per flow, as specified by the project requirements.
   - Runs Per Experiment:
      - We used 3 runs per experiment, as specified by the project requirements.  
- ECN Configuration:
   - Enabled Explicit Congestion Notification for all TCP connections.
      - Required for DCTCP to work properly.
      - Applied to bottlneck link in both directions.
- Buffer Configuration:
   - We utilized 256 KB socket buffers for the sender and receiver
   - By experimentation we found that 256 KB buffers were large enough so that our buffers do not become a bottleneck 
- Port Configuration:
   - We utilized two different ports for the destination, one as 8331 and one as 8332
   - This was done to avoid conflicts with sending to the destination
- Network Addressing:
   - Each link utilizes a different subnet in the 10.1.x.0/24 range
   - We also utilized static routing with Ipv4GlobalRoutingHelper 
- Measurement Methodology:
   - Throughput Calculation:
      - (bytes received * 8) / (completion time) / (1024 * 1024) [Mbps]
   - Flow Completion Time:
      - Measured as the difference in time between first packet transmission and last packet reception
   - Standard Deviation Calculation:
      - We implemented a custom function to calculate the standard deviation across the three runs
      - For each metric, we compute the mean of the three values, then calculate the square root of the average squared difference from the mean
