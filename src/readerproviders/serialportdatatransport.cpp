/**
 * \file serialportdatatransport.cpp
 * \author Maxime C. <maxime-dev@islog.com>
 * \brief Serial port data transport.
 */

#include "logicalaccess/myexception.hpp"
#include "logicalaccess/readerproviders/serialportdatatransport.hpp"
#include "logicalaccess/cards/readercardadapter.hpp"
#include "logicalaccess/bufferhelper.hpp"


namespace logicalaccess
{
	SerialPortDataTransport::SerialPortDataTransport(const std::string& portname) : d_isAutoDetected(false)
	{
		d_port.reset(new SerialPortXml(portname));

#ifndef UNIX
		d_portBaudRate = CBR_9600;
#else
		d_portBaudRate = B9600;
#endif
		m_timeout = 50;
	}

	SerialPortDataTransport::~SerialPortDataTransport()
	{
	}

	bool SerialPortDataTransport::connect()
	{
		bool ret = false;

		startAutoDetect();

		EXCEPTION_ASSERT_WITH_LOG(d_port, LibLogicalAccessException, "No serial port configured !");
		EXCEPTION_ASSERT_WITH_LOG(d_port->getSerialPort()->deviceName() != "", LibLogicalAccessException, "Serial port name is empty ! Auto-detect failed !");

		if (!d_port->getSerialPort()->isOpen())
		{
			d_port->getSerialPort()->open();
			configure();
			ret = true;
		}
		else
		{
			ret = true;
		}

		return ret;
	}

	void SerialPortDataTransport::disconnect()
	{
		if (d_port->getSerialPort()->isOpen())
		{
			d_port->getSerialPort()->close();
		}
	}

	bool SerialPortDataTransport::isConnected()
	{
		return d_port->getSerialPort()->isOpen();
	}

	std::string SerialPortDataTransport::getName() const
	{
		std::string name;

		if (d_port)
		{
			name = d_port->getSerialPort()->deviceName();
		}

		return name;
	}

	void SerialPortDataTransport::send(const std::vector<unsigned char>& data)
	{
		if (data.size() > 0)
		{
			LOG(LogLevel::COMS) << "Send command: " << BufferHelper::getHex(data);
			d_port->getSerialPort()->write(data);
		}
	}

	std::vector<unsigned char> SerialPortDataTransport::receive(long int timeout)
	{
		std::vector<unsigned char> res, tmpbuf;
		long double start = std::clock();
		bool end = false;

		while ((d_port->getSerialPort()->read(tmpbuf, 256) > 0 || res.size() == 0x00)
			&& !end)
		{
			res.insert(res.end(), tmpbuf.begin(), tmpbuf.end());
			end = std::clock() > start + timeout + 100;
		}
		LOG(LogLevel::COMS) << "Command response: " << BufferHelper::getHex(res);

		return res;
	}

	void SerialPortDataTransport::configure()
	{
		configure(d_port, Settings::getInstance()->IsConfigurationRetryEnabled);
	}

	void SerialPortDataTransport::configure(boost::shared_ptr<SerialPortXml> port, bool retryConfiguring)
	{
		EXCEPTION_ASSERT_WITH_LOG(port, LibLogicalAccessException, "No serial port configured !");
		EXCEPTION_ASSERT_WITH_LOG(port->getSerialPort()->deviceName() != "", LibLogicalAccessException, "Serial port name is empty ! Auto-detect failed !");

		try
		{
			unsigned long baudrate = getPortBaudRate();

			LOG(LogLevel::DEBUGS) << "Configuring serial port " << port->getSerialPort()->deviceName() << " - Baudrate " << baudrate << "...";
			port->getSerialPort()->setBaudrate(d_portBaudRate);
			port->getSerialPort()->setFlowControl(boost::asio::serial_port_base::flow_control::none);
			port->getSerialPort()->setCharacterSize(8);
			port->getSerialPort()->setParity(boost::asio::serial_port_base::parity::none);
			port->getSerialPort()->setStopBits(boost::asio::serial_port_base::stop_bits::one);

			port->getSerialPort()->setTimeout(m_timeout);
		}
		catch(std::exception& e)
		{
			if (retryConfiguring)
			{
				// Strange stuff is going here... by waiting and reopening the COM port (maybe for system cleanup), it's working !
				std::string portn = port->getSerialPort()->deviceName();
				LOG(LogLevel::WARNINGS) << "Exception received " << e.what() << " ! Sleeping "
					<< Settings::getInstance()->ConfigurationRetryTimeout << " milliseconds -> Reopen serial port "
					<< portn << " -> Finally retry  to configure...";
				std::this_thread::sleep_for(std::chrono::milliseconds(Settings::getInstance()->ConfigurationRetryTimeout));

				port->getSerialPort()->reopen();
				configure(port, false);
			}
		}
	}

	void SerialPortDataTransport::startAutoDetect()
	{
		if (d_port && d_port->getSerialPort()->deviceName() == "")
		{
			if (!Settings::getInstance()->IsAutoDetectEnabled)
			{
				LOG(LogLevel::INFOS) << "Auto detection is disabled through settings !";
				return;
			}

			LOG(LogLevel::INFOS) << "Serial port is empty ! Starting Auto COM Port Detection...";
			std::vector<boost::shared_ptr<SerialPortXml> > ports;
			if (SerialPortXml::EnumerateUsingCreateFile(ports) && !ports.empty() && getReaderUnit())
			{
				std::vector<unsigned char> cmd = getReaderUnit()->getPingCommand();
				if (cmd.size() > 0)
				{
					boost::shared_ptr<ReaderCardAdapter> rca = getReaderUnit()->getDefaultReaderCardAdapter();
					std::vector<unsigned char> wrappedcmd = rca->adaptCommand(cmd);
					bool found = false;
					for (std::vector<boost::shared_ptr<SerialPortXml> >::iterator i  = ports.begin(); i != ports.end() && !found; ++i)
					{
						try
						{
							LOG(LogLevel::INFOS) << "Processing port " << (*i)->getSerialPort()->deviceName() << "...";
							(*i)->getSerialPort()->open();
							configure((*i), false);
						
							
							
							d_port = (*i);
							std::vector<unsigned char> r = sendCommand(wrappedcmd, Settings::getInstance()->AutoDetectionTimeout);
							if (r.size() > 0)
							{
								LOG(LogLevel::INFOS) << "Reader found ! Using this COM port !";
								found = true;
							}
						}
						catch (std::exception& e)
						{
							LOG(LogLevel::ERRORS) << "Exception " << e.what();
						}

						if ((*i)->getSerialPort()->isOpen())
						{
							(*i)->getSerialPort()->close();
						}
					}

					if (!found)
					{
						LOG(LogLevel::INFOS) << "No reader found on COM port...";
					}
					else
					{
						d_isAutoDetected = true;
					}
				}
			}
			else
			{
				LOG(LogLevel::WARNINGS) << "No COM Port detected !";
			}
		}
	}

	void SerialPortDataTransport::serialize(boost::property_tree::ptree& parentNode)
	{
		boost::property_tree::ptree node;

		node.put("<xmlattr>.type", getTransportType());
		node.put("PortBaudRate", d_portBaudRate);
		node.put("TimeOut", m_timeout);
		d_port->serialize(node);

		parentNode.add_child(getDefaultXmlNodeName(), node);
	}

	void SerialPortDataTransport::unSerialize(boost::property_tree::ptree& node)
	{
		d_portBaudRate = node.get_child("PortBaudRate").get_value<unsigned long>();
		m_timeout = node.get_child("TimeOut").get_value<int>();
		d_port.reset(new SerialPortXml());
		d_port->unSerialize(node.get_child(d_port->getDefaultXmlNodeName()));
	}

	std::string SerialPortDataTransport::getDefaultXmlNodeName() const
	{
		return "SerialPortDataTransport";
	}
}
