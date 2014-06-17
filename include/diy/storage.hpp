#ifndef DIY_STORAGE_HPP
#define DIY_STORAGE_HPP

#include <string>
#include <map>
#include <fstream>

#include <unistd.h>     // used for mkstemp()
#include <cstdio>       // used for remove()

#include "serialization.hpp"

namespace diy
{
  class ExternalStorage
  {
    public:
      virtual int   put(BinaryBuffer& bb)           =0;
      virtual void  get(int i, BinaryBuffer& bb)    =0;
      virtual void  destroy(int i)                  =0;
  };

  class FileStorage: public ExternalStorage
  {
    public:
                    FileStorage(const std::string& filename_template = "/tmp/DIY.XXXXXX"):
                      filename_template_(filename_template), count_(0)      {}

      virtual int   put(BinaryBuffer& bb)
      {
        std::string     filename = filename_template_.c_str();
        mkstemp(const_cast<char*>(filename.c_str()));

        std::cout << "FileStorage::put(): " << filename << std::endl;
        std::ofstream   out(filename.c_str(), std::ios::binary);

        int sz = bb.buffer.size();
        out.write(&bb.buffer[0], sz);
        bb.clear();

        FileRecord  fr = { sz, filename };
        filenames_[count_] = fr;
        return count_++;
      }

      virtual void   get(int i, BinaryBuffer& bb)
      {
        FileRecord      fr = filenames_[i];
        filenames_.erase(i);

        std::cout << "FileStorage::get(): " << fr.name << std::endl;
        std::ifstream   in(fr.name.c_str(), std::ios::binary);

        bb.buffer.resize(fr.size);
        in.read(&bb.buffer[0], fr.size);

        remove(fr.name.c_str());
      }

      virtual void  destroy(int i)
      {
        FileRecord      fr = filenames_[i];
        filenames_.erase(i);
        remove(fr.name.c_str());
      }

                    ~FileStorage()
      {
        for (std::map<int,FileRecord>::const_iterator it = filenames_.begin(); it != filenames_.end(); ++it)
        {
          remove(it->second.name.c_str());
        }
      }

    private:
      struct FileRecord
      {
        int             size;
        std::string     name;
      };

    private:
      int                           count_;
      std::string                   filename_template_;
      std::map<int, FileRecord>     filenames_;
  };
}

#endif
