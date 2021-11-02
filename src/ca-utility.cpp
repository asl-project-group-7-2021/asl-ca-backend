#define STRINGIZER(arg)     #arg
#define STR_VALUE(arg)      STRINGIZER(arg)
#define CA_PATH_STRING STR_VALUE(CA_PATH)
#define CONFIG_PATH_STRING STR_VALUE(CONFIG_PATH)
#define OPENSSL_PATH_STRING STR_VALUE(OPENSSL_PATH)

#include <iostream>
#include <string>
#include <stdlib.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <regex>

namespace fs = std::filesystem;
using namespace std;

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

int main(int argc, char *argv[]){

  if(argc <= 1){
    cout << "ca-utility requires at least three arguments!" << endl;
    cout << "Usage: ./ca-utility /path/to/openssl.conf (sign|revoke) target" << endl;
    return 1;
  }
  string configPath = CONFIG_PATH_STRING;
  string caPath = CA_PATH_STRING;
  string opensslPath = OPENSSL_PATH_STRING;

  string caPathIndexFile = caPath + "index.txt";
  string caPathSerialFile = caPath + "serial";
  string caCrlNumberFile = caPath + "crlnumber";
  string caCrlFile = caPath + "crl/crl.pem";

  string caPathUserKeys = caPath + "private/users/";
  string caPathRequests = caPath + "requests/";
  string caPathCertificates = caPath + "newcerts/";
  // certs/ doesn't seem to be used so
  //string caPathNewCertificates = caPath + "newcerts/";
  string caPathCRL = caPath + "crl/";

  // safe since no user input is used
  system(("mkdir -p '" + caPathUserKeys + "'").c_str());
  system(("mkdir -p '" + caPathRequests + "'").c_str());
  //system(("mkdir -p '" + caPathNewCertificates + "'").c_str());
  system(("mkdir -p '" + caPathCertificates + "'").c_str());
  system(("mkdir -p '" + caPathCRL + "'").c_str());

  fs::path index{ caPathIndexFile };
  if (!fs::exists(index)){
    system(("touch '" + caPathIndexFile + "'").c_str());
  }
  fs::path serial{ caPathSerialFile };
  if (!fs::exists(serial)){
    system(("echo '01' > '" + caPathSerialFile + "'").c_str());
  }
  fs::path crlNum{ caCrlNumberFile };
  if (!fs::exists(crlNum)){
    system(("echo '01' > '" + caCrlNumberFile + "'").c_str());
  }

  string command = argv[1];

  if(command == "update-crl"){
    execl(opensslPath.c_str(), "openssl", "ca", "-gencrl", "-out", caCrlFile.c_str(), "-config", configPath.c_str(), NULL);
    return 0;
  }else if(argc <= 2){
    cout << "ca-utility requires at least three arguments!" << endl;
    cout << "Usage: ./ca-utility /path/to/openssl.conf (generate|request|sign|revoke) target" << endl;
    return 1;
  }

  string targetString = argv[2];

  try {
      int target = stoi(targetString);

      if(command == "generate"){
        // read serial file
        string serialString = exec(("cat '" + caPathSerialFile + "'").c_str());
        try {
          int serial = stoi(serialString);
          if(target != serial){
            cout << "Serial file is at " << serial << ", you requested to generate " << target << endl;
            return 4;
          }
        } catch (std::exception const &e) {
          cout << "Serial file contains invalid content: " << serial << "!" << endl;
          return 5;
        }

        string output = caPathUserKeys + to_string(target) + ".key";
        fs::path p{ output };
        if (fs::exists(p)){
          cout << "File at path '" << output << "' already exists!" << endl;
          return 6;
        }

        pid_t c_pid = fork();

        if (c_pid == -1) {
            return 100;
        } else if (c_pid > 0) {
            // parent process
            // wait for child  to terminate
            wait(nullptr);
            // the user actually needs to have to private key so on generation return the contents!
            cout << exec(("cat '" + output + "'").c_str()) << endl;
            return 0;
        } else {
            // child process
            execl(opensslPath.c_str(), "openssl", "genrsa", "-out", output.c_str(), "4096", NULL);
            return 0;
        }

      }else if(command == "request"){
        // read serial file
        string serialString = exec(("cat '" + caPathSerialFile + "'").c_str());
        try {
          int serial = stoi(serialString);
          if(target != serial){
            cout << "Serial file is at " << serial << ", you requested to generate " << target << endl;
            return 7;
          }
        } catch (std::exception const &e) {
          cout << "Serial file contains invalid content: " << serial << "!" << endl;
          return 8;
        }

        string output = caPathRequests + to_string(target) + ".csr";
        fs::path p{ output };
        if (fs::exists(p)){
          cout << "File at path '" << output << "' already exists!" << endl;
          return 9;
        }

        string input = caPathUserKeys + to_string(target) + ".key";
        fs::path p2{ input };
        if (!fs::exists(p2)){
          cout << "File at path '" << input << "' does not exists!" << endl;
          return 10;
        }

        //-subj "/C=GB/ST=London/L=London/O=Global Security/OU=IT Department/CN=example.com
        if(argc <= 3){
          cout << "For creating a certificate request, a common name has to be passed!" << endl;
          return 11;
        }

        string commonName = argv[3];
        regex e ("[A-z]*@imovies\\.ch");
        if (regex_match(commonName,e)){
          string subject = "/C=CH/ST=Zurich/L=Zurich/O=iMovies/OU=IT/CN=" + commonName;
          // IMPORTANT: use execl, prevents execution of arbitrary shell commands!
          execl(opensslPath.c_str(), "openssl", "req", "-new", "-key", input.c_str(), "-out", output.c_str(), "-subj", subject.c_str(), NULL);
          return 0;
        }else{
          cout << "The passed common name is invalid!" << endl;
          return 12;
        }
      }else if(command == "sign"){
        string serialString = exec(("cat '" + caPathSerialFile + "'").c_str());
        try {
          int serial = stoi(serialString);
          if(target != serial){
            cout << "Serial file is at " << serial << ", you requested to generate " << target << endl;
            return 13;
          }
        } catch (std::exception const &e) {
          cout << "Serial file contains invalid content: " << serial << "!" << endl;
          return 14;
        }

        string input = caPathRequests + to_string(target) + ".csr";
        fs::path p{ input };
        if (!fs::exists(p)){
          cout << "File at path '" << input << "' does not exists!" << endl;
          return 15;
        }

        string output = caPathCertificates + to_string(target) + ".crt";

        fs::path p2{ output };
        if (fs::exists(p2)){
          cout << "File at path '" << input << "' already exists!" << endl;
          return 16;
        }

        execl(opensslPath.c_str(), "openssl", "ca", "-batch", "-in", input.c_str(), "-config", configPath.c_str(), NULL);
        return 0;
      }else if(command == "revoke"){
        string input = caPathCertificates + (target < 10 ? "0" + to_string(target) : to_string(target)) + ".pem";
        fs::path p{ input };
        if (!fs::exists(p)){
          cout << "File at path '" << input << "' does not exists!" << endl;
          return 17;
        }

        execl(opensslPath.c_str(), "openssl", "ca", "-revoke", input.c_str(), "-config", configPath.c_str(), NULL);
        return 0;
      }else{
        cout << "Unknown command '" << command << "'" << endl;
        return 3;
      }

  } catch (std::exception const &e) {
    cout << "Invalid target was passed!" << endl;
    return 2;
  }

  return 0;
}