#include <fstream>
#include <string>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/uinteger.h"
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ns3simulator");

NodeContainer source, destination, router;

// We set a flow time of 120 seconds, to ensure that all experiments have enough time to completely transfer the data
double flow_time = 120.0;
double start_time = 0.0;
int d1_port = 8331, d2_port = 8332;

// Set max bytes to 50 MB
uint maxBytes = 50 * 1024 * 1024;

float calculate_sd(float a, float b, float c) {
    float mean = (a + b + c) / 3;
    float summation = pow(a - mean, 2) + pow(b - mean, 2) + pow(c - mean, 2);
    float sd = sqrt(summation / 3);
    return sd;
}

void data_transfer(Ptr<Node> src, Ptr<Node> dest, Address sinkAddress, uint16_t sinkPort, 
                  std::string tcp_version, double start_time) {

    // Dynamically configure TCP variant by setting the socket type on a specific node
    std::string nodePath = "/NodeList/" + std::to_string(src->GetId()) + "/$ns3::TcpL4Protocol/SocketType";

    if (tcp_version == "TcpBic") {
        Config::Set(nodePath, TypeIdValue(TcpBic::GetTypeId()));
    } else if (tcp_version == "TcpDctcp") {
        Config::Set(nodePath, TypeIdValue(TcpDctcp::GetTypeId()));
    }
  
    double endtime = start_time + flow_time;
    BulkSendHelper sourceHelper("ns3::TcpSocketFactory", sinkAddress);

    // MaxBytes ensures flow doesn't exceed the 50MB limit
    sourceHelper.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", 
                                    InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer dest_container = packetSinkHelper.Install(dest);
    ApplicationContainer source_container = sourceHelper.Install(src);

    dest_container.Start(Seconds(start_time));
    dest_container.Stop(Seconds(endtime));
    source_container.Start(Seconds(start_time));
    source_container.Stop(Seconds(endtime));
}

void create_topology() {
    source.Create(2);
    destination.Create(2);
    router.Create(2);
}

void configure_network(PointToPointHelper& pointToPoint, Ipv4AddressHelper& address,
                     Ipv4InterfaceContainer& i1, Ipv4InterfaceContainer& i2,
                     Ipv4InterfaceContainer& i3, Ipv4InterfaceContainer& i4,
                     Ipv4InterfaceContainer& i5) {
    // Set buffer sizes - 256 KB
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(262144));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(262144));
    
    // Add DCTCP specific configurations
    Config::SetDefault("ns3::TcpDctcp::DctcpShiftG", DoubleValue(0.0625));  
    Config::SetDefault("ns3::TcpDctcp::DctcpAlphaOnInit", DoubleValue(1.0)); 
    Config::SetDefault("ns3::TcpDctcp::UseEct0", BooleanValue(true));

    InternetStackHelper internet;
    internet.Install(source);
    internet.Install(destination);
    internet.Install(router);

    NodeContainer link1 = NodeContainer(source.Get(0), router.Get(0));
    NodeContainer link2 = NodeContainer(source.Get(1), router.Get(0));

    // Central bottleneck link between the two routers 
    NodeContainer link3 = NodeContainer(router.Get(0), router.Get(1));

    NodeContainer link4 = NodeContainer(router.Get(1), destination.Get(1));
    NodeContainer link5 = NodeContainer(router.Get(1), destination.Get(0));

    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    
    // Low point-to-point delay to increase RTT times - low latency simulates data center
    pointToPoint.SetChannelAttribute("Delay", StringValue(".25ms"));


    address.SetBase("10.1.1.0", "255.255.255.0");
    NetDeviceContainer s1linkr1 = pointToPoint.Install(link1);
    i1 = address.Assign(s1linkr1);

    address.SetBase("10.1.2.0", "255.255.255.0");
    NetDeviceContainer s2linkr1 = pointToPoint.Install(link2);
    i2 = address.Assign(s2linkr1);

    address.SetBase("10.1.3.0", "255.255.255.0");
    NetDeviceContainer r1linkr2 = pointToPoint.Install(link3);
    i3 = address.Assign(r1linkr2);

    address.SetBase("10.1.4.0", "255.255.255.0");
    NetDeviceContainer r2linkd1 = pointToPoint.Install(link5);
    i4 = address.Assign(r2linkd1);

    address.SetBase("10.1.5.0", "255.255.255.0");
    NetDeviceContainer r2linkd2 = pointToPoint.Install(link4);
    i5 = address.Assign(r2linkd2);
  
    // Reset existing queue disc before applying RED
    TrafficControlHelper tch;
    tch.Uninstall(r1linkr2);

    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));

    TrafficControlHelper tchRed;

    // Parameter choices are explained in the README
    tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                         "MinTh", DoubleValue(20),
                         "MaxTh", DoubleValue(60),
                         "QW", DoubleValue(0.0625),
                         "LinkBandwidth", StringValue("1Gbps"),
                         "LinkDelay", StringValue(".25ms"),
                         "UseEcn", BooleanValue(true),
                         "Gentle", BooleanValue(true),
                         "FengAdaptive", BooleanValue(true),
                         "MeanPktSize", UintegerValue(1500));

    // Set the queue size limit 
    Config::SetDefault("ns3::RedQueueDisc::MaxSize", 
                      QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 200)));

    // Configure RedQueueDisc only on the bottleneck link to simulate Active Queue Management
    tchRed.Install(r1linkr2.Get(0));
    tchRed.Install(r1linkr2.Get(1));

    // Needed for static routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
}

void run_experiments(Address destinationIP1, Address destinationIP2) {
    // Experiment 1
    for (int i = 0; i < 3; i++, start_time += flow_time) {
        data_transfer(source.Get(0), destination.Get(0), destinationIP1, d1_port, "TcpBic", start_time);
    }
    
    // Experiment 2
    for (int i = 0; i < 3; i++, start_time += flow_time) {
        data_transfer(source.Get(0), destination.Get(0), destinationIP1, d1_port, "TcpBic", start_time);
        data_transfer(source.Get(1), destination.Get(1), destinationIP2, d2_port, "TcpBic", start_time);
    }
    
    // Experiment 3
    for (int i = 0; i < 3; i++, start_time += flow_time) {
        data_transfer(source.Get(0), destination.Get(0), destinationIP1, d1_port, "TcpDctcp", start_time);
    }
    
    // Experiment 4
    for (int i = 0; i < 3; i++, start_time += flow_time) {
        data_transfer(source.Get(0), destination.Get(0), destinationIP1, d1_port, "TcpDctcp", start_time);
        data_transfer(source.Get(1), destination.Get(1), destinationIP2, d2_port, "TcpDctcp", start_time);
    }
    
    // Experiment 5
    for (int i = 0; i < 3; i++, start_time += flow_time) {
        data_transfer(source.Get(0), destination.Get(0), destinationIP1, d1_port, "TcpBic", start_time);
        data_transfer(source.Get(1), destination.Get(1), destinationIP2, d2_port, "TcpDctcp", start_time);
    }
}

void collect_and_analyze_data(std::vector<double>& tputsexp1, std::vector<double>& tputsexp2,
                            std::vector<double>& tputs2exp2, std::vector<double>& tputsexp3,
                            std::vector<double>& tputsexp4, std::vector<double>& tputs2exp4,
                            std::vector<double>& tputsexp5, std::vector<double>& tputs2exp5,
                            std::vector<double>& ftimeexp1, std::vector<double>& ftimeexp2,
                            std::vector<double>& ftime2exp2, std::vector<double>& ftimeexp3,
                            std::vector<double>& ftimeexp4, std::vector<double>& ftime2exp4,
                            std::vector<double>& ftimeexp5, std::vector<double>& ftime2exp5) {
    // Install flow monitor on all nodes
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
   
    // Run the simulation and clean up
    Simulator::Stop(Seconds(2000));
    Simulator::Run();
    Simulator::Destroy();
  
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    int flowid = 1;

    for (int i = 0; i < 15; i++) {
        FlowMonitor::FlowStats fs = stats[flowid];
        double time_taken = fs.timeLastRxPacket.GetSeconds() - fs.timeFirstTxPacket.GetSeconds();
        double curr_throughput = fs.rxBytes * 8.0 / time_taken / (1024 * 1024);    
        if (flowid < 7) {
            // Experiment 1
            tputsexp1.push_back(curr_throughput);
            ftimeexp1.push_back(time_taken);
            std::cout << "exp1 -thpt - " << curr_throughput << std::endl;
            std::cout << "exp1 -time - " << time_taken << std::endl;
        } else if (flowid < 18) {
            // Experiment 2
            tputsexp2.push_back(curr_throughput);
            ftimeexp2.push_back(time_taken);
            std::cout << "exp2 -thpt link 1 - " << curr_throughput << std::endl;
            std::cout << "exp2-time link 1 - " << time_taken << std::endl;
           
            FlowMonitor::FlowStats fs = stats[flowid + 1];
            flowid += 2;
            time_taken = fs.timeLastRxPacket.GetSeconds() - fs.timeFirstTxPacket.GetSeconds();
            curr_throughput = fs.rxBytes * 8.0 / time_taken / (1024 * 1024);
            tputs2exp2.push_back(curr_throughput);
            ftime2exp2.push_back(time_taken);
            std::cout << "exp2 -thpt link 2 - " << curr_throughput << std::endl;
            std::cout << "exp2-time link 2 - " << time_taken << std::endl;
        } else if (flowid < 25) {
            // Experiment 3
            tputsexp3.push_back(curr_throughput);
            ftimeexp3.push_back(time_taken);
            std::cout << "exp3 -thpt - " << curr_throughput << std::endl;
            std::cout << "exp3 -time - " << time_taken << std::endl;
        } else if (flowid < 37) {
            // Experiment 4
            tputsexp4.push_back(curr_throughput);
            ftimeexp4.push_back(time_taken);
            std::cout << "exp4 -thpt link 1 - " << curr_throughput << std::endl;
            std::cout << "exp4-time link 1 - " << time_taken << std::endl;
            
            FlowMonitor::FlowStats fs = stats[flowid + 1];
            flowid += 2;
            time_taken = fs.timeLastRxPacket.GetSeconds() - fs.timeFirstTxPacket.GetSeconds();
            curr_throughput = fs.rxBytes * 8.0 / time_taken / (1024 * 1024);
            tputs2exp4.push_back(curr_throughput);
            ftime2exp4.push_back(time_taken);
            std::cout << "exp4 -thpt link 2 - " << curr_throughput << std::endl;
            std::cout << "exp4-time link 2 - " << time_taken << std::endl;
        } else {
            // Experiment 5
            tputsexp5.push_back(curr_throughput);
            ftimeexp5.push_back(time_taken);
            std::cout << "exp5 -thpt link 1 - " << curr_throughput << std::endl;
            std::cout << "exp5-time link 1 - " << time_taken << std::endl;
            
            FlowMonitor::FlowStats fs = stats[flowid + 1];
            flowid += 2;
            time_taken = fs.timeLastRxPacket.GetSeconds() - fs.timeFirstTxPacket.GetSeconds();
            curr_throughput = fs.rxBytes * 8.0 / time_taken / (1024 * 1024);
            tputs2exp5.push_back(curr_throughput);
            ftime2exp5.push_back(time_taken);
            std::cout << "exp5 -thpt link 2 - " << curr_throughput << std::endl;
            std::cout << "exp5-time link 2 - " << time_taken << std::endl;
        }
        flowid += 2;
    }
    monitor->SerializeToXmlFile("ns3simulator.flowmon", true, true);
}

void calculate_statistics(const std::vector<double>& tputsexp1, const std::vector<double>& tputsexp2,
                        const std::vector<double>& tputs2exp2, const std::vector<double>& tputsexp3,
                        const std::vector<double>& tputsexp4, const std::vector<double>& tputs2exp4,
                        const std::vector<double>& tputsexp5, const std::vector<double>& tputs2exp5,
                        const std::vector<double>& ftimeexp1, const std::vector<double>& ftimeexp2,
                        const std::vector<double>& ftime2exp2, const std::vector<double>& ftimeexp3,
                        const std::vector<double>& ftimeexp4, const std::vector<double>& ftime2exp4,
                        const std::vector<double>& ftimeexp5, const std::vector<double>& ftime2exp5,
                        float th_means[], float th_sds[], float afct_means[], float afct_sds[]) {
    // Calculate means of throughput
    th_means[1] = (tputsexp1[1] + tputsexp1[2] + tputsexp1[0]) / 3;
    th_means[2] = (tputsexp2[1] + tputsexp2[2] + tputsexp2[0]) / 3;
    th_means[3] = (tputs2exp2[1] + tputs2exp2[2] + tputs2exp2[0]) / 3;
    th_means[4] = (tputsexp3[1] + tputsexp3[2] + tputsexp3[0]) / 3;
    th_means[5] = (tputsexp4[1] + tputsexp4[2] + tputsexp4[0]) / 3;
    th_means[6] = (tputs2exp4[1] + tputs2exp4[2] + tputs2exp4[0]) / 3;
    th_means[7] = (tputsexp5[1] + tputsexp5[2] + tputsexp5[0]) / 3;
    th_means[8] = (tputs2exp5[1] + tputs2exp5[2] + tputs2exp5[0]) / 3;

    // Calculate standard deviations of throughput
    th_sds[1] = calculate_sd(tputsexp1[1], tputsexp1[2], tputsexp1[0]);
    th_sds[2] = calculate_sd(tputsexp2[1], tputsexp2[2], tputsexp2[0]);
    th_sds[3] = calculate_sd(tputs2exp2[1], tputs2exp2[2], tputs2exp2[0]);
    th_sds[4] = calculate_sd(tputsexp3[1], tputsexp3[2], tputsexp3[0]);
    th_sds[5] = calculate_sd(tputsexp4[1], tputsexp4[2], tputsexp4[0]);
    th_sds[6] = calculate_sd(tputs2exp4[1], tputs2exp4[2], tputs2exp4[0]);
    th_sds[7] = calculate_sd(tputsexp5[1], tputsexp5[2], tputsexp5[0]);
    th_sds[8] = calculate_sd(tputs2exp5[1], tputs2exp5[2], tputs2exp5[0]);

    // Calculate means of average flow completion time
    afct_means[1] = (ftimeexp1[1] + ftimeexp1[2] + ftimeexp1[0]) / 3;
    afct_means[2] = (ftimeexp2[1] + ftimeexp2[2] + ftimeexp2[0]) / 3;
    afct_means[3] = (ftime2exp2[1] + ftime2exp2[2] + ftime2exp2[0]) / 3;
    afct_means[4] = (ftimeexp3[1] + ftimeexp3[2] + ftimeexp3[0]) / 3;
    afct_means[5] = (ftimeexp4[1] + ftimeexp4[2] + ftimeexp4[0]) / 3;
    afct_means[6] = (ftime2exp4[1] + ftime2exp4[2] + ftime2exp4[0]) / 3;
    afct_means[7] = (ftimeexp5[1] + ftimeexp5[2] + ftimeexp5[0]) / 3;
    afct_means[8] = (ftime2exp5[1] + ftime2exp5[2] + ftime2exp5[0]) / 3;

    // Calculate standard deviations of average flow completion time
    afct_sds[1] = calculate_sd(ftimeexp1[1], ftimeexp1[2], ftimeexp1[0]);
    afct_sds[2] = calculate_sd(ftimeexp2[1], ftimeexp2[2], ftimeexp2[0]);
    afct_sds[3] = calculate_sd(ftime2exp2[1], ftime2exp2[2], ftime2exp2[0]);
    afct_sds[4] = calculate_sd(ftimeexp3[1], ftimeexp3[2], ftimeexp3[0]);
    afct_sds[5] = calculate_sd(ftimeexp4[1], ftimeexp4[2], ftimeexp4[0]);
    afct_sds[6] = calculate_sd(ftime2exp4[1], ftime2exp4[2], ftime2exp4[0]);
    afct_sds[7] = calculate_sd(ftimeexp5[1], ftimeexp5[2], ftimeexp5[0]);
    afct_sds[8] = calculate_sd(ftime2exp5[1], ftime2exp5[2], ftime2exp5[0]);
}

void write_output(const std::vector<double>& tputsexp1, const std::vector<double>& tputsexp2,
                 const std::vector<double>& tputs2exp2, const std::vector<double>& tputsexp3,
                 const std::vector<double>& tputsexp4, const std::vector<double>& tputs2exp4,
                 const std::vector<double>& tputsexp5, const std::vector<double>& tputs2exp5,
                 const std::vector<double>& ftimeexp1, const std::vector<double>& ftimeexp2,
                 const std::vector<double>& ftime2exp2, const std::vector<double>& ftimeexp3,
                 const std::vector<double>& ftimeexp4, const std::vector<double>& ftime2exp4,
                 const std::vector<double>& ftimeexp5, const std::vector<double>& ftime2exp5,
                 const float th_means[], const float th_sds[], 
                 const float afct_means[], const float afct_sds[]) {
    // Open output file for writing
    std::ofstream outputFile("tcp_hchench_adlapp.csv");

    // Create proper headers 
    outputFile << "exp,r1_s1,r2_s1,r3_s1,avg_s1,std_s1,unit_s1,r1_s2,r2_s2,r3_s2,avg_s2,std_s2,unit_s2,\n";

    // Write data for throughput - experiment 1
    outputFile << "th_1," << tputsexp1[0] << "," << tputsexp1[1] << "," << tputsexp1[2] << "," 
               << th_means[1] << "," << th_sds[1] << "," << "Mbps," << ",,,,,,\n";

    // Write data for throughput - experiment 2
    outputFile << "th_2," << tputsexp2[0] << "," << tputsexp2[1] << "," << tputsexp2[2] << "," 
               << th_means[2] << "," << th_sds[2] << "," << "Mbps," << tputs2exp2[0] << "," 
               << tputs2exp2[1] << "," << tputs2exp2[2] << "," << th_means[3] << "," << th_sds[3] << ",Mbps,\n";

    // Write data for throughput - experiment 3
    outputFile << "th_3," << tputsexp3[0] << "," << tputsexp3[1] << "," << tputsexp3[2] << "," 
               << th_means[4] << "," << th_sds[4] << "," << "Mbps," << ",,,,,,\n";

    // Write data for throughput - experiment 4
    outputFile << "th_4," << tputsexp4[0] << "," << tputsexp4[1] << "," << tputsexp4[2] << "," 
               << th_means[5] << "," << th_sds[5] << "," << "Mbps," << tputs2exp4[0] << "," 
               << tputs2exp4[1] << "," << tputs2exp4[2] << "," << th_means[6] << "," << th_sds[6] << ",Mbps,\n";

    // Write data for throughput - experiment 5
    outputFile << "th_5," << tputsexp5[0] << "," << tputsexp5[1] << "," << tputsexp5[2] << "," 
               << th_means[7] << "," << th_sds[7] << "," << "Mbps," << tputs2exp5[0] << "," 
               << tputs2exp5[1] << "," << tputs2exp5[2] << "," << th_means[8] << "," << th_sds[8] << ",Mbps,\n";

    // Write data for average flow completion time - experiment 1
    outputFile << "afct_1," << ftimeexp1[0] << "," << ftimeexp1[1] << "," << ftimeexp1[2] << "," 
               << afct_means[1] << "," << afct_sds[1] << "," << "sec," << ",,,,,,\n";

    // Write data for average flow completion time - experiment 2
    outputFile << "afct_2," << ftimeexp2[0] << "," << ftimeexp2[1] << "," << ftimeexp2[2] << "," 
               << afct_means[2] << "," << afct_sds[2] << "," << "sec," << ftime2exp2[0] << "," 
               << ftime2exp2[1] << "," << ftime2exp2[2] << "," << afct_means[3] << "," << afct_sds[3] << ",sec,\n";

    // Write data for average flow completion time - experiment 3
    outputFile << "afct_3," << ftimeexp3[0] << "," << ftimeexp3[1] << "," << ftimeexp3[2] << "," 
               << afct_means[4] << "," << afct_sds[4] << "," << "sec," << ",,,,,,\n";

    // Write data for average flow completion time - experiment 4
    outputFile << "afct_4," << ftimeexp4[0] << "," << ftimeexp4[1] << "," << ftimeexp4[2] << "," 
               << afct_means[5] << "," << afct_sds[5] << "," << "sec," << ftime2exp4[0] << "," 
               << ftime2exp4[1] << "," << ftime2exp4[2] << "," << afct_means[6] << "," << afct_sds[6] << ",sec,\n";

    // Write data for average flow completion time - experiment 5
    outputFile << "afct_5," << ftimeexp5[0] << "," << ftimeexp5[1] << "," << ftimeexp5[2] << "," 
               << afct_means[7] << "," << afct_sds[7] << "," << "sec," << ftime2exp5[0] << "," 
               << ftime2exp5[1] << "," << ftime2exp5[2] << "," << afct_means[8] << "," << afct_sds[8] << ",sec,\n";
    
    // Close output file
    outputFile.close();
}

int main(int argc, char *argv[]) {
    // Network helper classes for configuring point-to-point links and addresses
    PointToPointHelper pointToPoint;
    Ipv4AddressHelper address;

    // Containers to store network interface addresses for the different nodes
    Ipv4InterfaceContainer i1, i2, i3, i4, i5;
    
    // Vectors to store throughput and flow time for multiple experiments
    std::vector<double> tputsexp1, tputsexp2, tputs2exp2, tputsexp3, tputsexp4, 
                       tputs2exp4, tputsexp5, tputs2exp5;
    std::vector<double> ftimeexp1, ftimeexp2, ftime2exp2, ftimeexp3, ftimeexp4, 
                       ftime2exp4, ftimeexp5, ftime2exp5;

    // Arrays used to store statistics for throughput and average flow completion time
    float th_means[9], th_sds[9], afct_means[9], afct_sds[9];

    // Create nodes and links
    create_topology();

    // Configure network with point-to-point links, assigning IP addresses
    configure_network(pointToPoint, address, i1, i2, i3, i4, i5);
    
    // Destination addresses 
    Address destinationIP1 = InetSocketAddress(i4.GetAddress(1), d1_port);
    Address destinationIP2 = InetSocketAddress(i5.GetAddress(1), d2_port);
   
    run_experiments(destinationIP1, destinationIP2);

  
    collect_and_analyze_data(tputsexp1, tputsexp2, tputs2exp2, tputsexp3, tputsexp4,
                           tputs2exp4, tputsexp5, tputs2exp5, ftimeexp1, ftimeexp2,
                           ftime2exp2, ftimeexp3, ftimeexp4, ftime2exp4, ftimeexp5,
                           ftime2exp5);
    calculate_statistics(tputsexp1, tputsexp2, tputs2exp2, tputsexp3, tputsexp4,
                        tputs2exp4, tputsexp5, tputs2exp5, ftimeexp1, ftimeexp2,
                        ftime2exp2, ftimeexp3, ftimeexp4, ftime2exp4, ftimeexp5,
                        ftime2exp5, th_means, th_sds, afct_means, afct_sds);
    write_output(tputsexp1, tputsexp2, tputs2exp2, tputsexp3, tputsexp4,
                tputs2exp4, tputsexp5, tputs2exp5, ftimeexp1, ftimeexp2,
                ftime2exp2, ftimeexp3, ftimeexp4, ftime2exp4, ftimeexp5,
                ftime2exp5, th_means, th_sds, afct_means, afct_sds);

    return 0;
}
