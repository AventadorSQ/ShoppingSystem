#pragma once
#include "httplib.h"
#include<iostream>
#include<fstream>
#include<sstream>
#include<string>
#include<thread>
#include<vector>
#include<unordered_map>
#include<boost/filesystem.hpp>
#include<boost/algorithm/string.hpp>

#define CLIENT_BACKUP_DIR "backup"
#define CLIENT_BACKUP_INFO_FILE "backuplist.txt"
#define RANGE_MAX_SIZE (10 << 20)
#define SERVER_IP "8.136.212.245"
#define SERVER_PORT (16300)
//#define SERVER_IP "192.168.166.145"
//#define SERVER_PORT (10000)
#define BACKUP_DTRECTORY_PATH "/list/"
#define BACKUP_FORMAT "text/plain"

namespace bf = boost::filesystem;

//创建一个块数据上传类
class ThrBackUp
{
private:
	std::string _file;
	int64_t _range_start;
	int64_t _range_len;
public:
	bool _res;
public:
	//初始化直接将传进来的文件块大小，起始地址设置好
	ThrBackUp(const std::string& file, int64_t start, int64_t len)
		:_res(true)
		, _file(file)
		, _range_len(len)
		, _range_start(start)
	{}
	//
	bool Start()
	{
		std::stringstream tmp;
		tmp << "上传文件：[" << _file << "] range位置: [" << _range_start << "-" << _range_start + _range_len - 1 << "] 块大小：(" << _range_len << ")" << std::endl;
		std::cout << tmp.str();
		std::ifstream path(_file, std::ios::binary);
		if (!path.is_open())
		{
			_res = false;
			std::cerr << "range backup file " << _file << " failed\n";
			return false;
		}
		//跳转到指定位置seekg
		path.seekg(_range_start, std::ios::beg);
		std::string body;
		body.resize(_range_len);
		path.read(&body[0], _range_len);
		if (!path.good())
		{
			_res = false;
			path.close();
			std::cerr << "read file " << _file << " range data failed\n";
			return false;
		}
		path.close();

		//组织http格式上传数据

		bf::path name(_file);
		std::string uri = BACKUP_DTRECTORY_PATH + name.filename().string();
		//实例化一个httplib客户端对象
		httplib::Client cli(SERVER_IP, SERVER_PORT);
		//http请求头信息
		httplib::Headers hdr;
		hdr.insert(std::make_pair("Content-Length", std::to_string(_range_len)));
		std::stringstream temp;
		temp << "bytes=" << _range_start << "-" << (_range_start + _range_len - 1);
		hdr.insert(std::make_pair("Range", temp.str().c_str()));
		//通过实例化的Client向服务端发送PUT请求
		auto rsp = cli.Put(uri.c_str(), hdr, body, BACKUP_FORMAT);
		if (rsp && rsp->status == 200)
		{
			std::cout << "分块上传成功" << std::endl;
			_res = true;
		}
		else
		{
			std::cout << "分块上传失败" << std::endl;
			_res = false;
		}
		return true;
	}
};

class CloudClient
{
private:
	std::unordered_map<std::string, std::string> _backup_list;
public:

	CloudClient()
	{
		bf::path file(CLIENT_BACKUP_DIR);
		if (!bf::exists(file))
		{
			bf::create_directory(file);
		}
	}
private:
	bool GetBackupInfo()
	{
		bf::path path(CLIENT_BACKUP_INFO_FILE);
		if (!bf::exists(path))
		{
			std::cerr << "保存上传的文件信息的[" << CLIENT_BACKUP_INFO_FILE << "]文件不存在!!!" << std::endl;
			return false;
		}
		int64_t fsize = bf::file_size(path);
		if (fsize == 0)
		{
			std::cerr << "no have backup info" << std::endl;
			return false;
		}
		std::string body;
		body.resize(fsize);
		std::ifstream file(CLIENT_BACKUP_INFO_FILE, std::ios::binary);
		if (!file.is_open())
		{
			std::cerr << "list file open error" << std::endl;
			return false;
		}
		file.read(&body[0], fsize);
		if (!file.good())
		{
			std::cerr << "read list file body error" << std::endl;
			return false;
		}
		file.close();
		std::vector<std::string> list;
		//split将分割的数据放进list中
		//第三个参数问题
		boost::split(list, body, boost::is_any_of("\n"));
		for (auto e : list)
		{
			size_t pos = e.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = e.substr(0, pos);
			std::string val = e.substr(pos + 1);
			_backup_list[key] = val;
		}
		return true;
	}
	bool SetBackupInfo()
	{
		std::string body;
		for (auto e : _backup_list)
		{
			body += e.first + " " + e.second + "\n";
		}

		std::ofstream file(CLIENT_BACKUP_INFO_FILE, std::ios::binary);
		if (!file.is_open())
		{
			std::cerr << "open list file error" << std::endl;
			return false;
		}

		file.write(&body[0], body.size());
		if (!file.good())
		{
			file.close();
			std::cerr << "set backup info error" << std::endl;
			return false;
		}
		file.close();
		return true;
	}
	bool BackupDirListen(const std::string& path)
	{
		bf::directory_iterator item_begin(CLIENT_BACKUP_DIR);
		bf::directory_iterator item_end;
		for (; item_begin != item_end; ++item_begin)
		{
			if (bf::is_directory(item_begin->status()))
			{
				BackupDirListen(item_begin->path().string());
				continue;
			}

			if (FileIsNeedBackup(item_begin->path().string()) == false)
			{
				continue;
			}
			std::cerr << "文件：[" << item_begin->path().string() << "] 需要备份\n";
			if (PutFileData(item_begin->path().string()) == false)
			{
				continue;
			}

			AddBackInfo(item_begin->path().string());
		}
		return true;
	}
	bool AddBackInfo(const std::string& file)
	{
		std::string etag;
		if (GetFileEtag(file, etag) == false)
		{
			return false;
		}
		_backup_list[file] = etag;
		return true;
	}
	bool FileIsNeedBackup(const std::string& file)
	{
		std::string etag;
		if (GetFileEtag(file, etag) == false)
		{
			return false;
		}
		auto it = _backup_list.find(file);
		if (it != _backup_list.end() && it->second == etag)
		{
			return false;
		}
		return true;
	}
	bool GetFileEtag(const std::string& file, std::string& etag)
	{
		bf::path path(file);
		if (!bf::exists(path))
		{
			std::cerr << "get file etag error" << std::endl;
			return false;
		}
		int64_t fsize = bf::file_size(path);
		int64_t mtime = bf::last_write_time(path);
		std::stringstream temp;
		temp << std::hex << fsize << "-" << std::hex << mtime;
		etag = temp.str();
		return true;
	}
	bool PutFileData(const std::string& file)
	{
		//1.获取文件大小
		int64_t fsize = bf::file_size(file);
		//2.计算一共分的块数。得到每块大小以及起始位置
		//3.循环创建线程，在线程中上传文件数据
		int64_t count = fsize / RANGE_MAX_SIZE;
		std::vector<ThrBackUp> thr_res;
		int64_t DataRemainder = 0;
		DataRemainder = fsize % RANGE_MAX_SIZE;
		if (DataRemainder != 0)
		{
			count++;
		}
		std::cerr << "文件大小：[" << fsize << "],上传块个数：[" << count << "]" << std::endl;
		std::vector<std::thread> thr_list;
		for (int64_t i = 0; i < count; i++)
		{
			int64_t range_start = i * RANGE_MAX_SIZE;
			int64_t range_end = (i + 1) * RANGE_MAX_SIZE - 1;
			if (i == (count - 1))
			{
				if (DataRemainder != 0)
				{
					range_end = i * RANGE_MAX_SIZE + DataRemainder - 1;
				}
			}
			int64_t range_len = range_end - range_start + 1;
			ThrBackUp backup_info(file, range_start, range_len);
			thr_res.push_back(backup_info);
		}
		//创建线程
		for (int i = 0; i < count; i++)
		{
			thr_list.push_back(std::thread(thr_start, &thr_res[i]));
		}
		//4.等待所有线程退出，判断文件上传结果
		//其中有几个块上传失超过刷新时间继续传输，可以优化
		bool ret = true;
		for (int i = 0; i < count; i++)
		{
			thr_list[i].join();
			if (thr_res[i]._res == true)
			{
				continue;
			}
			ret = false;
		}
		//5.上传成功，添加文件的备份信息
		if (ret == false)
		{
			std::cerr << "文件：[" << file << "] 上传失败" << std::endl;
			return false;
		}
		//AddBackInfo(file);
		std::cerr << "文件：[" << file << "] 上传成功\n" << std::endl;
		return true;
	}

	static void thr_start(ThrBackUp* backup_info)
	{
		backup_info->Start();
		return;
	}
public:
	bool Start()
	{
		GetBackupInfo();
		while (1)
		{
			BackupDirListen(CLIENT_BACKUP_DIR);
			SetBackupInfo();
			Sleep(2000);
		}
		return true;
	}
};
