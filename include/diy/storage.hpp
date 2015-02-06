#ifndef DIY_STORAGE_HPP
#define DIY_STORAGE_HPP

#include <string>
#include <map>
#include <fstream>

#include <unistd.h>     // mkstemp() on Mac
#include <cstdlib>      // mkstemp() on Linux
#include <cstdio>       // remove()
#include <fcntl.h>

#include "serialization.hpp"
#include "thread.hpp"

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
#ifdef __MACH__
        // TODO: figure out how to open with O_SYNC
        int fh = mkstemp(const_cast<char*>(filename.c_str()));
#else
        int fh = mkostemp(const_cast<char*>(filename.c_str()), O_WRONLY | O_SYNC);
#endif

        //std::cout << "FileStorage::put(): " << filename << std::endl;

        int sz = bb.buffer.size();
        write(fh, &bb.buffer[0], sz);
        fsync(fh);
        close(fh);
        bb.wipe();

        int res = (*count_.access())++;
        FileRecord  fr = { sz, filename };
        (*filenames_.access())[res] = fr;

        return res;
      }

      virtual void   get(int i, BinaryBuffer& bb)
      {
        FileRecord      fr;
        {
          CriticalMapAccessor accessor = filenames_.access();
          fr = (*accessor)[i];
          accessor->erase(i);
        }

        //std::cout << "FileStorage::get(): " << fr.name << std::endl;

        bb.buffer.resize(fr.size);
        int fh = open(fr.name.c_str(), O_RDONLY | O_SYNC, 0600);
        read(fh, &bb.buffer[0], fr.size);
        close(fh);

        remove(fr.name.c_str());
      }

      virtual void  destroy(int i)
      {
        FileRecord      fr;
        {
          CriticalMapAccessor accessor = filenames_.access();
          fr = (*accessor)[i];
          accessor->erase(i);
        }
        remove(fr.name.c_str());
      }

                    ~FileStorage()
      {
        for (FileRecordMap::const_iterator it =  filenames_.const_access()->begin();
                                           it != filenames_.const_access()->end();
                                         ++it)
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

      typedef           std::map<int, FileRecord>                   FileRecordMap;
      typedef           critical_resource<FileRecordMap>            CriticalMap;
      typedef           CriticalMap::accessor                       CriticalMapAccessor;

    private:
      std::string                   filename_template_;
      critical_resource<int>        count_;
      CriticalMap                   filenames_;
  };
}

#endif
