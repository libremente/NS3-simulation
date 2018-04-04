/* WIFI_TCP_simulation.cc
* RUN AS = ./waf --run "WIFI_TCP_simulation --filein=schedfile --tracing --interference"
* --PrintHelp for knowing the arguments

================== NETWORK SETUP ===========================
*
*  [10.1.5.0] (wifi)                    n2 -> TS
*                                      /
*  *    *    *    *                [10.1.2.0] (ptp)
*  |    |    |    |   [10.1.1.0]     /
* n5   n6   n7   n0 -------------- n1-[10.1.3.0]--n3 -> IUS
* ITC  IUC  TC   AP      (ptp)       \
*                                  [10.1.4.0] (ptp)
*                                      \
*                                       n4 -> ITS
*
* ITC/S: Interference TCP Client/Server - WifiSTA
* IUC/S: Interference UDP Client/Server - WifiSTA
* TC/S: Test Client/Server - WifiSTA
* AP: Access Point
============================================================
*/

// General includes
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include "ns3/config-store.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"

// Namespace
using namespace ns3;
// Name defined for the logger
std::string loggerName = "WIFI_TCP_simulation";
// REMEMBER to execute `$export NS_LOG=<logger_name>=level_info`
NS_LOG_COMPONENT_DEFINE (loggerName);

// Globals
// Define
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE -1
#endif

#define FB_SIZE       8

// Vector
std::vector<uint32_t>   pktDimensionVector;

// Constants
const uint8_t SKT_CONTROL_FLAGS = 0;

/**
 * Class to handle ad-hoc runtime exceptions.
 * Throw as: throw customException("<message>")
 */
class customException : public std::exception
{
    std::string message;
public:
    customException(std::string m) : message(m) {}
    ~customException() throw() {}
    virtual const char* what() const throw()
  {
    return message.c_str();
  }
};

/***************** TCP SERVER *************
* Class TCPReceiver
* TCP server application implementation
* This class is used as a TCP Server that:
* -> Does the normal server job (bind/listen/accept)
* -> Does a buffer read (readn) of the incoming packets till reaching a predetermined size
* -> Sends back a "feedback" packet containing the timing of the receive
*/
class TCPReceiver : public Application
{
public:
  TCPReceiver();
  virtual ~TCPReceiver ();
  void Setup(uint16_t serverPort, Time startTime);

private:
  virtual void StartApplication();
  virtual void StopApplication();
  uint32_t GetTotalRx();
  void DoDispose();
  void HandleRead (Ptr<Socket> socket);
  void HandlePeerClose (Ptr<Socket> socket);
  void HandlePeerError (Ptr<Socket> socket);
  void HandleAccept (Ptr<Socket> socket, const Address& from);
  void HandleSuccessClose(Ptr<Socket> socket);
  // FeedBack handling
  void MyFBSend();
  void SendNowFB();
  void SetFill (char* fill);

  Ptr<Socket>             m_socket;
  uint32_t                m_totalRx;
  std::list<Ptr<Socket> > m_socketList;
  TypeId                  m_tid;
  uint32_t                m_packetsReceived;
  Time                    m_startTime;
  uint8_t*                m_recvBuffer;
  char*                   m_timeStringContainer;
  int32_t                 m_readReturn;
  int32_t                 m_toComplete;
  uint32_t                m_dataSize;
  uint8_t*                m_data;
  uint16_t                m_port;
  EventId                 m_sendEvent;
  uint32_t                m_FBSent;
};

// Constructor
TCPReceiver::TCPReceiver ()
{
  NS_LOG_FUNCTION (this);
  m_socket          = 0;
  m_totalRx         = 0;
  m_packetsReceived = 0;
  m_dataSize        = 0;
  m_FBSent          = 0;
  m_readReturn      = 0;
  m_toComplete      = 0;
}

// Destructor
TCPReceiver::~TCPReceiver()
{
  NS_LOG_FUNCTION (this);
  //free(m_recvBuffer);
}

/** Setup function - 2 arguments
* The socket is NOT passed to the function. The socket will be allocated in the Start Application
* function.
*/
void
TCPReceiver::Setup(uint16_t serverPort,  Time startTime)
{
  m_port      = serverPort;
  m_startTime = startTime;
}

// Getters
uint32_t
TCPReceiver::GetTotalRx ()
{
  NS_LOG_FUNCTION (this);
  return m_totalRx;
}

void TCPReceiver::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_socketList.clear ();

  // chain up
  Application::DoDispose ();
}

/** Application Methods
* Called at time specified by Start
*/
void
TCPReceiver::StartApplication ()
{
  NS_LOG_FUNCTION (this);

  // Create the socket if not already existing
  if (m_socket == 0)
  {
    TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
    m_socket = Socket::CreateSocket (GetNode(), tid);
    InetSocketAddress listenAddress = InetSocketAddress (Ipv4Address::GetAny(), m_port);
    m_socket->Bind (listenAddress);
    m_socket->Listen();
  }

  // AcceptCallback
  m_socket->SetAcceptCallback (
    MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
    MakeCallback (&TCPReceiver::HandleAccept, this)
  );
  // RecvCallback
  m_socket->SetRecvCallback (MakeCallback (&TCPReceiver::HandleRead, this));
  // CloseCallback
  m_socket->SetCloseCallbacks (
    MakeCallback (&TCPReceiver::HandlePeerClose, this),
    MakeCallback (&TCPReceiver::HandlePeerError, this)
  );
}

/** Stop Application
 * Called by simulator to stop the server
 */
void
TCPReceiver::StopApplication ()
{
  NS_LOG_FUNCTION (this);
  while(!m_socketList.empty ()) //these are accepted sockets, close them
  {
    Ptr<Socket> acceptedSocket = m_socketList.front ();
    m_socketList.pop_front ();
    acceptedSocket->Close ();
  }
  if (m_socket)
  {
    m_socket->Close ();
    m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
  }
}

/** Handle Accept
 * Function called as soon as there is an accept
 */
void TCPReceiver::HandleAccept (Ptr<Socket> socket, const Address& from)
{
  NS_LOG_FUNCTION (this << socket << from);
  socket->SetRecvCallback (MakeCallback (&TCPReceiver::HandleRead, this));
  // Saving socket in a list
  m_socketList.push_back (socket);
  // CloseCallback
  socket->SetCloseCallbacks(MakeCallback (&TCPReceiver::HandleSuccessClose, this),
                       MakeNullCallback<void, Ptr<Socket> > () );
}

/** Handle Success Close
 * This function is called as soon as a close pkt arrives
 */
void TCPReceiver::HandleSuccessClose(Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_LOGIC ("Client close received");
  socket->Close();
  socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > () );
  socket->SetCloseCallbacks(MakeNullCallback<void, Ptr<Socket> > (),
                            MakeNullCallback<void, Ptr<Socket> > () );
}

/** Handle Read
 * This function is called when something is available at the TCP level.
 * It handles the incremental read waiting for the exact amount of bytes
 * before proceeding. Next it calles the "feedback" function which sends
 * back to the sender a packet containing the time of the receive
 */
void
TCPReceiver::HandleRead (Ptr<Socket> socket) {

  NS_LOG_FUNCTION(this << socket);

  // Get the dimension of the *current* packet
  int32_t currentPktSize = pktDimensionVector[m_packetsReceived];

  // Initialize variables to this value
  if(m_toComplete == 0) {
    m_toComplete = currentPktSize;
    m_recvBuffer = (uint8_t*) malloc((size_t)currentPktSize);
  }

  // While loop to perform a buffered read
  while(( m_readReturn = socket->Recv(m_recvBuffer, (uint32_t) m_toComplete , SKT_CONTROL_FLAGS)) >= 0 )
  {
    if (m_readReturn == 0)
    {
      // CASE 0: Recv returns 0 if there is nothing to be read
      break;
    }
    else
    {
      if((m_readReturn != 0) && (m_readReturn != m_toComplete)){
        // CASE 1: PARTIAL read.
        m_toComplete = m_toComplete - m_readReturn;
      }
      else if((m_readReturn != 0) && (m_readReturn == m_toComplete)){
        // CASE 2: Read COMPLETED. Update counters and move on
        m_toComplete = 0;
        m_totalRx += currentPktSize;
        Time rightNow = Simulator::Now() - m_startTime;
        m_packetsReceived++;
        // Log print
        NS_LOG_INFO( "[R] #" << m_packetsReceived
          << " - Dimension: "
          << currentPktSize
          << " RECV at: "
          << rightNow.GetSeconds()
          << "s - TOTAL: "
          << m_totalRx << "bytes"
        );

        // Call MyFBSend to schedule the Feedback packet send
        m_timeStringContainer = (char*) malloc ((size_t)FB_SIZE);
        snprintf(m_timeStringContainer, FB_SIZE, "%f", rightNow.GetSeconds());
        SetFill(m_timeStringContainer);

        // Free buffers
        free(m_recvBuffer);
        free(m_timeStringContainer);

        // Call FeedBack Send
        MyFBSend();
      }
    }
  }
}

/** My FB Send
 * Function to schedule the SendNowFB call
 */
void
TCPReceiver::MyFBSend(void)
{
  // Scheduling 'now' (0 seconds) but this can be changed to give a timeout
  m_sendEvent = Simulator::Schedule (Seconds(.0), &TCPReceiver::SendNowFB, this);
}

/** Set Fill
 * Function to fill the new packets to prepare the send
 */
void
TCPReceiver::SetFill (char* fill)
{
  NS_LOG_FUNCTION (fill);
  uint32_t dataSize = sizeof(fill);
  m_data = new uint8_t [dataSize];
  m_dataSize = dataSize;
  // Actually copying the content inside the buffer
  memcpy (m_data, fill, dataSize);
}

/** Send Now FeedBack
 * Function that actually sends the FeedBack packet
 */
void
TCPReceiver::SendNowFB(void)
{
  NS_LOG_FUNCTION_NOARGS ();

  Ptr<Packet> feedback;
  if (m_dataSize)
  {
    // SetFill has been called
    feedback = Create<Packet> (m_data, m_dataSize);
    delete[] m_data;
  }
  // TODO: Implement else: if SetFill has not been called what can we do? Write SimulatorNow() in pkt?
  /*else
  {
    // SetFill has NOT been called
    feedback = Create<Packet> (m_size);
  }*/

  // Retry the connected socket
  if(m_socketList.size() != 0) {
    Ptr <Socket> sock = m_socketList.front();
    Time rightNow = Simulator::Now() - m_startTime;
    m_FBSent++;
    NS_LOG_INFO("[F]-> #"
                << m_FBSent
                << " - Feedback SENT from server at: "
                << rightNow.GetSeconds()
                << "s of size "
                << feedback->GetSize()
    );

    // Send
    sock->Send(feedback);
  }
}

/** Handle Peer Close
 * Function to handle the close
 */
void TCPReceiver::HandlePeerClose (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}

void TCPReceiver::HandlePeerError (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}



/******************* TCP CLIENT ******************/
/** Class TCPSender
 * TCP client application implementation
 */
class TCPSender: public Application
{
public:
  TCPSender ();
  virtual ~TCPSender();
  void Setup (
      Ptr<Socket> socket,
      Address address,
      Time startTime,
      Time stopTime,
      Time threshInterval,
      std::string fileName,
      std::string nameStatFile,
      std::string nameThreshFile
  );

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);
  void MySendPacket (void);
  void SendNow(void);
  void ConnectionSucceeded(Ptr<Socket>);
  void ConnectionFailed(Ptr<Socket>);
  void HandleReadFB(Ptr<Socket> socket);
  void scheduleNextCalculation();
  void calculateThreshold();
  void stopCalculation();
  void printStats();

  Ptr<Socket>         m_socket;
  Address             m_peer;
  EventId             m_sendEvent;
  bool                m_running;
  uint32_t            m_packetsSent;
  Time                m_start;
  Time                m_stop;
  Time                m_threshInterval;
  uint32_t            m_totalTx;
  EventId             m_calculateThresholdEvent;
  uint32_t            m_oldSize;

  // Structures
  std::list<double>   m_pktDelayList;
  std::map<uint32_t, double>  mappaRecv;
  std::map<uint32_t, double>  mappaSent;
  std::map<double, double>    mappaThresh;

  // Handle feedbacks
  int32_t             m_readReturn;
  int32_t             m_toComplete;
  uint8_t             m_recvBuffer[FB_SIZE];
  uint32_t            m_FBReceived;

  // Stats
  std::string         m_nameStatFile;
  std::string         m_nameThreshFile;
};

/** Constructor
*/
TCPSender::TCPSender ()
{
  m_socket          = 0;
  m_running         = false;
  m_packetsSent     = 0;
  m_FBReceived      = 0;
  // initialize return to FB_SIZE
  m_readReturn      = FB_SIZE;
  m_toComplete      = FB_SIZE;
  m_oldSize         = 0;
  m_threshInterval  = Seconds(0.);
}

/** Destructor
*/
TCPSender::~TCPSender()
{
  m_socket = 0;
}

/** Setup
 * Function that allows the initial setup
 */
void
TCPSender::Setup (
    Ptr<Socket> socket,
    Address address,
    Time startTime,
    Time stopTime,
    Time threshInterval,
    std::string fileName,
    std::string nameStatFile,
    std::string nameThreshFile
){
  m_socket  = socket;
  m_peer    = address;
  m_start   = startTime;
  m_stop    = stopTime;
  m_threshInterval = threshInterval;
  m_nameStatFile = nameStatFile;
  m_nameThreshFile = nameThreshFile;

  // Read file and save in two lists
  double dim   = 0;
  double delay = 0;
  std::string   tempString;

  try{
    std::ifstream filePacketsTimings (fileName.c_str(), std::ifstream::in);
    // throw custom exception if file cannot be opened
    if(!filePacketsTimings) throw customException("[!] Error opening file! Check that input file exists!");
    filePacketsTimings.exceptions( std::ifstream::badbit );
      while (getline(filePacketsTimings, tempString)) {
        if (tempString.empty()) {
          break;
        }
        else {
          std::istringstream tmp(tempString);
          tmp >> dim >> delay;
          pktDimensionVector.push_back((uint32_t)dim);
          m_pktDelayList.push_back(delay);
        }
      }
    filePacketsTimings.close();
  }
  // catch if 'badbit' is set
  catch(std::ifstream::failure errorMessage){
    std::cout << "Error opening file. Check that file " << fileName << " exists in current folder! Error message: " << errorMessage.what() << std::endl;
    exit(EXIT_FAILURE);
  }
  // catch specialized exception
  catch(customException& e) {
    std::cout <<  e.what() << std::endl;
    exit(EXIT_FAILURE);
  }
}

/** Start Application
 * Function that starts the TCP Client application
 */
void
TCPSender::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  // Initialize
  m_running     = true;
  m_packetsSent = 0;

  // Create the socket
  if( InetSocketAddress::IsMatchingType (m_peer) || PacketSocketAddress::IsMatchingType (m_peer))
  {
    m_socket->Bind ();
  }

  // Setup connection
  m_socket->Connect (m_peer);
  m_socket->SetAllowBroadcast (true);

  m_socket->SetConnectCallback (
      MakeCallback (&TCPSender::ConnectionSucceeded, this),
      MakeCallback (&TCPSender::ConnectionFailed, this));

  // Set callback since the client receives feedback packets
  m_socket->SetRecvCallback (MakeCallback (&TCPSender::HandleReadFB, this));

  // Threshold calculation: stop scheduling and scheduling function call
  Simulator::Schedule(m_stop, &TCPSender::stopCalculation, this);
  scheduleNextCalculation();

  // Call the default function to start scheduling packets send
  MySendPacket();
}

/** Service functions Connection Succeeded and failed
 */
void TCPSender::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  m_running = true;
}

void TCPSender::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}

/** Stop Application
 * Function to stop the application called by simulator from main()
 */
void
TCPSender::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
  {
    Simulator::Cancel (m_sendEvent);
  }

  if (m_socket)
  {
    m_socket->Close ();
  }

  // Call the statistics print
  printStats();
}

/* My Send Packet
 * Function to schedule the packet send
 */
void
TCPSender::MySendPacket (void)
{
  // Check on the packet list
  if(!m_pktDelayList.empty()){
    double pktDelay = m_pktDelayList.front();
    m_pktDelayList.pop_front();
    if (m_running)
    {
      m_sendEvent = Simulator::Schedule (Seconds(pktDelay), &TCPSender::SendNow, this);
    }
  }
}

/* Send Now
 * Function to actually send the packets
 */
void
TCPSender::SendNow()
{
  // Read the packet dimension
  if(m_packetsSent < pktDimensionVector.size()){
    double pktDim = pktDimensionVector[m_packetsSent];

    // Create packet
    Ptr<Packet> packet = Create<Packet> (pktDim);
    Time rightNow = Simulator::Now() - m_start;

    // Save send timing in proper map
    mappaSent[m_packetsSent++] = rightNow.GetSeconds();
    m_totalTx += pktDim;

    NS_LOG_INFO ("[S] #" << m_packetsSent
      << " - Dimension: "
      << pktDim
      << " SENT at: "
      << rightNow.GetSeconds()
      << "s"
      << " TOTAL: "
      << m_totalTx
      );

    // Send
    m_socket->Send(packet);
    MySendPacket();
  }
}

/* Handle Read Feedback
 * Function to handle the read of the feedbacks coming back from the server
 */
void
TCPSender::HandleReadFB (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION(this << socket);

  // Buffered read
  while(( m_readReturn = socket->Recv(m_recvBuffer, (uint32_t) m_readReturn , SKT_CONTROL_FLAGS)) >= 0 )
  {
    if (m_readReturn == 0)
    {
      // CASE 0: Recv returns 0 if there is nothing to be read
      m_readReturn = FB_SIZE;
      break;
    }
    else
    {
      if((m_readReturn != 0) && (m_readReturn != m_toComplete)){
        // CASE 1: PARTIAL read.
        m_readReturn = m_toComplete = FB_SIZE - m_readReturn;
        break;
      }
      else if( (m_readReturn != 0) && (m_readReturn == m_toComplete)){
        // CASE 2: Read COMPLETED. Update counters and move on
        m_readReturn = m_toComplete = FB_SIZE;

        Time rightNow = Simulator::Now() - m_startTime;
        char charToPrint[FB_SIZE];
        memcpy(charToPrint, m_recvBuffer, FB_SIZE);
        double doubleContainer = atof(charToPrint);

        // Populate map
        mappaRecv[m_FBReceived++] = doubleContainer;

        // Log print
        NS_LOG_INFO( "[F]<- #" << m_FBReceived
                     << " - Feedback RECEIVED from Client at: "
                     << rightNow.GetSeconds()
                     << " of size: "
                     << m_readReturn
        );
      }
    }
  } // end while
}

// Threshold Calculation
/** Schedule Next Calculation
 * Schedules the call to calculateThreshold function
 */
void TCPSender::scheduleNextCalculation ()
{
  if(m_running) {
    m_calculateThresholdEvent = Simulator::Schedule(m_threshInterval, &TCPSender::calculateThreshold, this);
  }
}

/** Calculate Threshold
 * Function to actually calculate the threshold
 * Now using SMA
 */
void TCPSender::calculateThreshold ()
{
  uint32_t pointer = 0, counter = 0, currentSize = 0;
  double difference = 0, average = 0;

  // Save size
  currentSize = (uint32_t) mappaRecv.size();

  if((!mappaRecv.empty()) && (currentSize != m_oldSize)) {
    if(m_oldSize == 0) {
      for (pointer = 0; pointer < currentSize; pointer++) {
        difference = mappaRecv[pointer] - mappaSent[pointer];
        average = average + difference;
        counter++;
      }
    }
    else if (m_oldSize != 0){
      for (pointer = m_oldSize; pointer < currentSize; pointer++) {
        difference = mappaRecv[pointer] - mappaSent[pointer];
        average = average + difference;
        counter++;
      }
    }
    if(counter != 0) {
      average = average / counter;
      mappaThresh[Simulator::Now().GetSeconds()] = average;
      NS_LOG_INFO("[T] Current threshold (average): " 
		      << average
		 );
    }
    else{
      mappaThresh[Simulator::Now().GetSeconds()] = 0.0;
    }
    // save size
    m_oldSize = currentSize;
  }
  // Reschedule
  TCPSender::scheduleNextCalculation();
}

/** Stop Calculation
 * Function to stop the threshold calculation at a given time
 * Stops also the future scheduled callbacks
 */
void TCPSender::stopCalculation(){
  if(m_calculateThresholdEvent.IsRunning()){
    Simulator::Cancel(m_calculateThresholdEvent);
  }
}

/** Print Stats
 * Functiont to print the statistics and the threshold values in two separated files
 * TODO this function should throw some errors
 */
void TCPSender::printStats() {
  std::ofstream statFile;

  try {
    statFile.open(m_nameStatFile.c_str(), std::ofstream::out);
    statFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);
    statFile << "#Blk\t Dim\t timeTx(s)\t timeRx(s)\t Difference(s)" << std::endl;

    if(pktDimensionVector.size() > mappaSent.size()){
      // Some packets still have to be sent
      NS_LOG_INFO("Some packets have not been sent. Try changing the simulation timings");
    }

    // TODO: check sizes
    if(mappaRecv.size() != 0) {
      size_t dim = mappaRecv.size();
      size_t i;

      for (i = 0; i < dim; i++) {
        // adding also the 'startClient' variable in order to have results coherent with timings in main()
        statFile << i + 1 // adding one just for not printing 0
        << "\t"
        << pktDimensionVector[i]
        << "\t"
        << std::fixed
        << std::setprecision(5)
        << mappaSent[i] + m_start.GetSeconds()
        << "\t\t"
        << std::setprecision(5)
        << mappaRecv[i] + m_start.GetSeconds()
        << "\t\t"
        << std::setprecision(5)
        << mappaRecv[i] - mappaSent[i]
        << std::endl;
      }
      statFile.close();
    }
    else{
      // TODO Reimplement this else
      std::cout << "[!] There was a problem creating the maps!" << std::endl;
    }
  }
  catch (std::ofstream::failure errorMessage) {
    std::cout << "Impossible to create and print to file " << m_nameStatFile << " for the following reasons: " <<
      errorMessage.what() << std::endl;
    exit(EXIT_FAILURE);
  }

  // save threshold in file
  std::ofstream threshFile;
  try {
    threshFile.open(m_nameThreshFile.c_str(), std::ofstream::out);
    threshFile.exceptions ( std::ofstream::failbit | std::ofstream::badbit );
    threshFile << "#Time(s)\t\t Threshold(s)" << std::endl;

    if(mappaThresh.size()!=0) {
      for (std::map<double, double>::iterator it = mappaThresh.begin(); it != mappaThresh.end(); ++it) {
        threshFile << std::fixed
        << std::setprecision(5)
        << it->first
        << "\t\t"
        << std::fixed
        << std::setprecision(5)
        << it->second
        << std::endl;
      }
      threshFile.close();
    }
    else{
      // TODO Reimplement this else
      std::cout << "[!] There was a problem creating the maps!" << std::endl;
    }
  }
  catch( std::ofstream::failure errorMessage ){
    std::cout << "Impossible to create and print to file " << m_nameStatFile << " for the following reasons: "
      << errorMessage.what() << std::endl;
    exit(EXIT_FAILURE);
  }
}


/**
 * Function for connecting to the Data Rate trace
 */
void
traceWifi(std::string context, uint32_t rate){
  //std::cout << "context: " << context << std::endl;
  std::cout << "[RATE] AT: " << Simulator::Now().GetSeconds() << " the new rate is: " << rate << std::endl;
}

/**
 * Function for connecting to the Drop trace
 */
void
DropTrace (std::string context, Ptr<const Packet> p)
{
  std::cout << "[DROP] AT " << Simulator::Now().GetSeconds() << " dropped packet " << p->GetSize() << std::endl;
}

/**
 * Function for connecting to the Queue trace
 */
void
tracepkt(std::string context, uint32_t oldValue, uint32_t newValue){
  //std::cout << "IN CONTEXT: " << context << std::endl;
  std::cout << "[QUEUE] AT " << Simulator::Now().GetSeconds()<< " old: " << oldValue << " new: " << newValue << std::endl;
}



/**
* Main function
*/
int
main (int argc, char *argv[])
{
  // Local variables
  bool verbose      = true;
  bool tracing      = false;
  bool interference = false;
  std::string         namefilein;
  std::string         nameStatFile = "simulation_results.dat";
  std::string         nameThreshFile = "thresh.dat";

  // Settings
  uint16_t serverPort   = 80;
  uint32_t nWifi        = 3;
  // Set timings
  Time startClient    = Seconds(1.);
  Time stopClient     = Seconds(300.);
  Time startServer    = Seconds(0.);
  Time stopServer     = Seconds(300.0);
  Time stopSimulation = Seconds(310.0);  // giving some more time than the server stop
  Time threshInterval = Seconds(0.25);   // threshold calculation interval
  Time startInterf    = Seconds(3.0);
  Time stopInterf     = Seconds(10.0);

  // Parse command line arguments
  CommandLine cmd;
  cmd.AddValue ("filein", "Name of file in", namefilein);
  cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);
  cmd.AddValue ("tracing", "Enable pcap tracing", tracing);
  cmd.AddValue ("startClient", "Set the client start time", startClient);
  cmd.AddValue ("interference", "Set the extra communications number", interference);
  cmd.Parse (argc,argv);

  // Create single nodes
  NodeContainer node_n0;
  node_n0.Create(1);
  NodeContainer node_n1;
  node_n1.Create(1);
  NodeContainer node_n2;
  node_n2.Create(1);
  NodeContainer node_n3;
  node_n3.Create(1);
  NodeContainer node_n4;
  node_n4.Create(1);

  // Create Point-to-point custom connections
  // n0 <-> n1
  PointToPointHelper connection_n0_n1;
  connection_n0_n1.SetDeviceAttribute ("DataRate", StringValue ("3.2Mbps"));
  connection_n0_n1.SetChannelAttribute ("Delay", StringValue ("15ms"));

  // n1 <-> n2
  PointToPointHelper connection_n1_n2;
  connection_n1_n2.SetDeviceAttribute("DataRate", StringValue ("100Mbps"));
  connection_n1_n2.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // n1 <-> n3
  PointToPointHelper connection_n1_n3;
  connection_n1_n3.SetDeviceAttribute("DataRate", StringValue ("100Mbps"));
  connection_n1_n3.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // n1 <-> n4
  PointToPointHelper connection_n1_n4;
  connection_n1_n4.SetDeviceAttribute("DataRate", StringValue ("100Mbps"));
  connection_n1_n4.SetChannelAttribute ("Delay", StringValue ("1ms"));

  // Create Net Device Containers
  // n0 <-> n1
  NodeContainer nodes_n0_n1;
  nodes_n0_n1.Add(node_n0);
  nodes_n0_n1.Add(node_n1);
  NetDeviceContainer netDeviceContainer_n0_n1 = connection_n0_n1.Install(nodes_n0_n1);

  // n1 <-> n2
  NodeContainer nodes_n1_n2;
  nodes_n1_n2.Add(node_n1);
  nodes_n1_n2.Add(node_n2);
  NetDeviceContainer netDeviceContainer_n1_n2 = connection_n1_n2.Install(nodes_n1_n2);

  // n1 <-> n3
  NodeContainer nodes_n1_n3;
  nodes_n1_n3.Add(node_n1);
  nodes_n1_n3.Add(node_n3);
  NetDeviceContainer netDeviceContainer_n1_n3 = connection_n1_n3.Install(nodes_n1_n3);

  // n1 <-> n4
  NodeContainer nodes_n1_n4;
  nodes_n1_n4.Add(node_n1);
  nodes_n1_n4.Add(node_n4);
  NetDeviceContainer netDeviceContainer_n1_n4 = connection_n1_n2.Install(nodes_n1_n4);

  // Creating wifi-stations
  NodeContainer wifiStaNodes;
  wifiStaNodes.Create (nWifi);          // 3 wifi STATIONS
  NodeContainer wifiApNode = node_n0;   // n0 is the AP - wifi but non STA

  // Wifi Channel -> Yet Another Network Simulator
  // See: http://cutebugs.net/files/wns2-yans.pdf
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper     wifiPhy     = YansWifiPhyHelper::Default ();
  wifiPhy.SetChannel (wifiChannel.Create ());
  WifiHelper            wifi        = WifiHelper::Default ();

  // AARF rate control algorithm
  // See: https://www.nsnam.org/docs/release/3.24/doxygen/classns3_1_1_aarf_wifi_manager.html
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");

  // Non-QOS MAC level
  NqosWifiMacHelper mac = NqosWifiMacHelper::Default ();

  // Wifi SSID
  Ssid ssid = Ssid ("ns-3-ssid");

  // Wifi STAtions configuration and install
  mac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));
  NetDeviceContainer staDevices;
  staDevices = wifi.Install (wifiPhy, mac, wifiStaNodes);

  // Wifi Access Point configuration and install
  mac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevices;
  apDevices = wifi.Install (wifiPhy, mac, wifiApNode);

  // Mobility of STAtions
  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                "MinX", DoubleValue(0.0),
                                "MinY", DoubleValue(0.0),
                                "DeltaX", DoubleValue(12.0),
                                "DeltaY", DoubleValue(12.0),
                                "GridWidth", UintegerValue(2),
                                "LayoutType", StringValue("RowFirst")
  );

  // STAs constant
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiStaNodes);

  // AP constant
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);

  // Install TCP/IP on all nodes
  InternetStackHelper stack;
  stack.Install(node_n1);
  stack.Install(node_n2);
  stack.Install(node_n3);
  stack.Install(node_n4);
  stack.Install (wifiApNode);   // This includes n0
  stack.Install (wifiStaNodes);

  // Assign addresses
  Ipv4AddressHelper address;
  // n0 <-> n1: Network 10.1.1.0
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer p2p_interfaces_n0_n1 = address.Assign(netDeviceContainer_n0_n1);
  // n1 <-> n2: Network 10.1.2.0
  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer p2p_interfaces_n1_n2 = address.Assign(netDeviceContainer_n1_n2);
  // n1 <-> n3: Network 10.1.3.0
  address.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer p2p_interfaces_n1_n3 = address.Assign(netDeviceContainer_n1_n3);
  // n1 <-> n4: Network 10.1.4.0
  address.SetBase("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer p2p_interfaces_n1_n4 = address.Assign(netDeviceContainer_n1_n4);
  // wifi: Network 10.1.5.0
  address.SetBase("10.1.5.0", "255.255.255.0");
  Ipv4InterfaceContainer wifiInterfaces;
  wifiInterfaces = address.Assign (apDevices);
  wifiInterfaces = address.Assign (staDevices);

  // Enable Global Routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Applications
  // TCP RECEIVER -> now on N2 which is directly linked (p2p) with n1
  Address receiverAddress (InetSocketAddress (p2p_interfaces_n1_n2.GetAddress(1), serverPort));
  Ptr<TCPReceiver> receiverApp = CreateObject<TCPReceiver>();
  receiverApp->Setup(serverPort, startClient);

  // Install and load
  node_n2.Get(0)->AddApplication(receiverApp);
  receiverApp->SetStartTime(startServer);
  receiverApp->SetStopTime(stopServer);

  // TCP SENDER -> now on the first Wifi STAtion
  Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (wifiStaNodes.Get(0), TcpSocketFactory::GetTypeId ());
  Ptr<TCPSender> senderApp = CreateObject<TCPSender> ();
  senderApp->Setup (ns3TcpSocket, receiverAddress, startClient, stopClient, threshInterval, namefilein, nameStatFile, nameThreshFile);

  // Install and load
  wifiStaNodes.Get(0)->AddApplication (senderApp);
  senderApp->SetStartTime (startClient);
  senderApp->SetStopTime (stopClient);

  // Interference
  /** If interference is set (call using "--interference") then activates 1 UDP and 1 TCP flow.
   * TCP Interference: Server in n4 and Client in one of the Wifi STAtions
   * UDP Interference: Server in n3 and Client in one of the Wifi STAtions
   */
  if(interference){
    // First Interference
    // UDP Echo Interference Server
    UdpEchoServerHelper udpInterferenceServer (8080);
    // Install and start
    ApplicationContainer udpInterferenceServerApp = udpInterferenceServer.Install (node_n3.Get(0));
    udpInterferenceServerApp.Start (startInterf);
    udpInterferenceServerApp.Stop (stopInterf);

    // UDP Echo Interference Client -> Sends to server and waits for same packet to come back
    UdpEchoClientHelper udpInterferenceClient (p2p_interfaces_n1_n3.GetAddress(1), 8080);
    udpInterferenceClient.SetAttribute ("MaxPackets", UintegerValue (300000));
    udpInterferenceClient.SetAttribute ("Interval", TimeValue (MilliSeconds (5.0)));
    udpInterferenceClient.SetAttribute ("PacketSize", UintegerValue (1024));
    // Install and start
    ApplicationContainer udpInterferenceClientApp = udpInterferenceClient.Install (wifiStaNodes.Get (1));
    udpInterferenceClientApp.Start (startInterf);
    udpInterferenceClientApp.Stop (stopInterf);

    // Second Interference
    // TCP Sink Interference Server
    uint16_t port = 8081;
    Address sinkLocalAddress(InetSocketAddress (p2p_interfaces_n1_n4.GetAddress(1), port));
    PacketSinkHelper tcpInterferenceServer ("ns3::TcpSocketFactory", sinkLocalAddress);
    // Install and start
    ApplicationContainer tcpInterferenceServerApp = tcpInterferenceServer.Install (nodes_n1_n4.Get(1));
    tcpInterferenceServerApp.Start (startInterf);
    tcpInterferenceServerApp.Stop (stopInterf);

    // TCP OnOff Client
    OnOffHelper tcpInterferenceClient ("ns3::TcpSocketFactory", Address(InetSocketAddress (p2p_interfaces_n1_n4.GetAddress(1), port)));
    tcpInterferenceClient.SetAttribute ("OnTime"    , StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    tcpInterferenceClient.SetAttribute ("OffTime"   , StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
    tcpInterferenceClient.SetAttribute ("PacketSize", UintegerValue(1024));
    // Install and start
    ApplicationContainer tcpInterferenceClientApp = tcpInterferenceClient.Install (wifiStaNodes.Get (2));
    tcpInterferenceClientApp.Start (startInterf);
    tcpInterferenceClientApp.Stop (stopInterf);
  }

  // Tracing
  /** If tracing is set (call using "--tracing") then activates the following traces:
   * - ASCII trace of n0<->n1 link (file .tr)
   * - PCAPs on all the other nodes
   * -- Convention for PCAP names: <file_name>-<node>-<device>.pcap
   */
  if(tracing){
    // Name
    std::string traceName = "TRACE";
    AsciiTraceHelper asciiTrace;
    connection_n0_n1.EnableAsciiAll(asciiTrace.CreateFileStream(traceName + "_ptp_n0_n1.tr"));
    // PCAP
    connection_n0_n1.EnablePcap(traceName + "_ptp_n0_n1_promiscuous", netDeviceContainer_n0_n1.Get(1), true);
  }

  // Output config store to txt format
  Config::SetDefault ("ns3::ConfigStore::Filename", StringValue ("WIFI_simulation_initial_attributes.txt"));
  Config::SetDefault ("ns3::ConfigStore::FileFormat", StringValue ("RawText"));
  Config::SetDefault ("ns3::ConfigStore::Mode", StringValue ("Save"));
  ConfigStore outputConfig;
  outputConfig.ConfigureDefaults ();
  outputConfig.ConfigureAttributes ();


  // Enabling traces
  // Trace wifi Rate Change (Aparf Wifi Manager)
  //Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/$ns3::AarfWifiManager/RateChange",
  //                MakeCallback(&traceWifi));
  // Trace AP Queue Drop packets
  //Config::Connect ("/NodeList/0/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/Drop",
  //                 MakeCallback (&DropTrace));
  // Trace AP number of packets in queue
  //Config::Connect ("/NodeList/0/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue",
  //                 MakeCallback (&tracepkt));


  // End
  Simulator::Stop (stopSimulation);
  Simulator::Run ();
  Simulator::Destroy ();

  return EXIT_SUCCESS;
}
