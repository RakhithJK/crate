// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

class Spec {
public:
  class OptDetails {
  public:
    virtual ~OptDetails() = 0;
  };
  class NetOptDetails : public OptDetails {
  public:
    NetOptDetails();
    static NetOptDetails* createDefault();
    typedef std::pair<unsigned,unsigned> PortRange;
    bool outboundWan;                 // allow outbound connections to WAN
    bool outboundLan;                 // allow outbound connections to LAN
    bool outboundHost;                // allow outbound connections to the host
    bool outboundDns;                 // allow DNS
    std::vector<std::pair<PortRange, PortRange>> inboundPortsTcp;
    std::vector<std::pair<PortRange, PortRange>> inboundPortsUdp;

    bool allowOutbound() const;
    bool allowInbound() const;
  };
  class TorOptDetails : public OptDetails {
  public:
    TorOptDetails();
    static TorOptDetails* createDefault();
    bool controlPort;                 // option to have control port created to be used from inside of the container
  };
  std::vector<std::string>                           baseKeep;
  std::vector<std::string>                           baseKeepWildcard;
  std::vector<std::string>                           baseRemove;

  std::vector<std::string>                           pkgInstall;              // 0..oo packages to install
  std::vector<std::pair<std::string, std::string>>   pkgLocalOverride;        // 0..oo packages to override
  std::vector<std::string>                           pkgAdd;                  // 0..oo packages to add
  std::vector<std::string>                           pkgNuke;                 // 0..oo packages to nuke, i.e. delete without regard of them being nominally used

  std::string                                        runCmdExecutable;        // 0..1 executables can be run
  std::string                                        runCmdArgs;              // can only be set when runCmdExecutable is set, always has a leading space when not blank
  std::vector<std::string>                           runServices;             // 0..oo services can be run

  std::vector<std::pair<std::string, std::string>>   dirsShare;               // any number of directories can be shared, {from -> to} mappings are elements
  std::vector<std::pair<std::string, std::string>>   filesShare;              // any number of files can be shared, {from -> to} mappings are elements

  std::map<std::string, std::shared_ptr<OptDetails>> options;                 // various options that this spec uses

  std::map<std::string, std::map<std::string, std::string>> scripts;          // by section, by script name

  Spec preprocess() const;
  void validate() const;
  bool optionExists(const char* opt) const;
  const NetOptDetails* optionNet() const;
  NetOptDetails* optionNetWr() const;
  const TorOptDetails* optionTor() const;
private:
  template<class OptDetailsClass>
  const OptDetailsClass* getOptionDetails(const char *opt) const;
  template<class OptDetailsClass>
  OptDetailsClass* getOptionDetailsWr(const char *opt) const;
};

Spec parseSpec(const std::string &fname);
