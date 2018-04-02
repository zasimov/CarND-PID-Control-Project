#include <uWS/uWS.h>
#include <iostream>
#include "json.hpp"
#include "PID.h"
#include <math.h>
#include <stdlib.h>
#include "journal.h"
#include <memory>


// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
std::string hasData(std::string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_last_of("]");
  if (found_null != std::string::npos) {
    return "";
  }
  else if (b1 != std::string::npos && b2 != std::string::npos) {
    return s.substr(b1, b2 - b1 + 1);
  }
  return "";
}


enum Opt {
  kOptUnknown = 0,
  kOptHelp,
  kOptTauP,
  kOptTauI,
  kOptTauD,
  kOptThrottleTauP,
  kOptThrottleTauI,
  kOptThrottleTauD,
  kOptMaxSpeed,
  kOptThrottle,
  kOptJournalFile,
};


Opt GetOpt(const std::string &opt) {
  if (opt == "-h" or opt == "--help") return kOptHelp;
  if (opt == "-p") return kOptTauP;
  if (opt == "-i") return kOptTauI;
  if (opt == "-d") return kOptTauD;
  if (opt == "-tp") return kOptThrottleTauP;
  if (opt == "-ti") return kOptThrottleTauI;
  if (opt == "-td") return kOptThrottleTauD;
  if (opt == "--max-speed") return kOptMaxSpeed;
  if (opt == "--throttle") return kOptThrottle;
  if (opt == "--journal") return kOptJournalFile;
  return kOptUnknown;
}


void Help() {
  std::cout << "pid [-p tau_p] [-i tau_i] [-d tau_d] [--journal FILENAME]" << std::endl << std::endl;
  std::cout << "Run PID controller." << std::endl;
  std::cout << "PID options:" << std::endl;
  std::cout << "    -p tau_p" << std::endl;
  std::cout << "    -i tau_i" << std::endl;
  std::cout << "    -d tau_d" << std::endl;
  std::cout << "    --max-speed SPEED" << std::endl;
  std::cout << "Throttle PID options:" << std::endl;
  std::cout << "    -tp tau_p" << std::endl;
  std::cout << "    -ti tau_i" << std::endl;
  std::cout << "    -td tau_d" << std::endl;
  std::cout << "    --throttle max_throttle" << std::endl;
  std::cout << "Journal options:" << std::endl;
  std::cout << "    --journal FILENAME" << std::endl;

  exit(1);
}


int shift(int argc, int i) {
  i++;

  if (i >= argc) {
    std::cerr << "error: expected value" << std::endl;
    Help();
    exit(1);
  }
  return i;
}


int main(int argc, char **argv)
{
  uWS::Hub h;

  double tau_p = 0.08, tau_i = 0.001, tau_d = 0.65;
  double t_tau_p = 1000000.0, t_tau_i = 0, t_tau_d = 100000.0;
  double max_throttle = 0.8;
  double max_speed = 40;

  bool use_journal = false;
  std::string journal_file_name = "";

  std::shared_ptr<Journal> journal;

  // read tau_ from command line
  int i = 1;
  while (i < argc) {
    switch (GetOpt(argv[i])) {
    case kOptHelp:
      Help();
      break;

    case kOptTauP:
      i = shift(argc, i);
      tau_p = atof(argv[i]);
      break;

    case kOptTauI:
      i = shift(argc, i);
      tau_i = atof(argv[i]);
      break;

    case kOptTauD:
      i = shift(argc, i);
      tau_d = atof(argv[i]);
      break;

    case kOptThrottleTauP:
      i = shift(argc, i);
      t_tau_p = atof(argv[i]);
      break;

    case kOptThrottleTauI:
      i = shift(argc, i);
      t_tau_i = atof(argv[i]);
      break;

    case kOptThrottleTauD:
      i = shift(argc, i);
      t_tau_d = atof(argv[i]);
      break;

    case kOptMaxSpeed:
      i = shift(argc, i);
      max_speed = atof(argv[i]);
      break;

    case kOptThrottle:
      i = shift(argc, i);
      max_throttle = atof(argv[i]);
      break;

    case kOptJournalFile:
      use_journal = true;
      i = shift(argc, i);
      journal_file_name = argv[i];
      break;

    default:
      std::cerr << "error: unknown " << argv[i] << std::endl;
      Help();
      break;
    }

    i++;
  }

  if (use_journal) {
    journal = std::make_shared<JournalFile>(journal_file_name);
  } else {
    journal = std::make_shared<Journal>();
    std::cout << "Journal will not be used." << std::endl;
  }

  journal->WriteHeader();

  // pid is a PID controller for steering angle
  PID pid(tau_p, tau_i, tau_d);
  std::cout << "Use PID " << tau_p << ", " << tau_i << ", " << tau_d << std::endl;

  PID t_pid(t_tau_p, t_tau_i, t_tau_d);
  std::cout << "Use Throttle PID " << t_tau_p << ", " << t_tau_i << ", " << t_tau_d << std::endl;

  h.onMessage([&pid, &t_pid, &journal, max_speed, max_throttle](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length, uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2')
    {
      auto s = hasData(std::string(data).substr(0, length));
      if (s != "") {
        auto j = json::parse(s);
        std::string event = j[0].get<std::string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          double cte = std::stod(j[1]["cte"].get<std::string>());
          double speed = std::stod(j[1]["speed"].get<std::string>());
          double angle = std::stod(j[1]["steering_angle"].get<std::string>());

	  pid.UpdateError(cte);
	  t_pid.UpdateError(cte);

	  double steer_value = -pid.TotalError();
	  if (steer_value > 1.0) {
	    steer_value = 1.0;
	  } else if (steer_value < -1.0) {
	    steer_value = -1.0;
	  }

	  double throttle_factor = speed / max_speed;
	  if (throttle_factor < 0.001) {
	    throttle_factor = 0.001;
	  }
	  throttle_factor = std::max(1.0, 1.5 * throttle_factor);
	  double throttle = (1 / throttle_factor)  * (max_throttle - pid.TotalError());
	  if (throttle > max_throttle) {
	    throttle = max_throttle;
	  } else if (throttle < -max_throttle) {
	    throttle = -max_throttle;
	  }

          // DEBUG
          std::cout << "CTE: " << cte << " Steering Value: " << steer_value << std::endl;
	  journal->Write(cte, steer_value, throttle);

          json msgJson;
          msgJson["steering_angle"] = steer_value;
          msgJson["throttle"] = throttle;
          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          std::cout << msg << std::endl;
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data, size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1)
    {
      res->end(s.data(), s.length());
    }
    else
    {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code, char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port))
  {
    std::cout << "Listening to port " << port << std::endl;
  }
  else
  {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }

  h.run();
}
