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
      virtual int   put(BinaryBuffer& bb)                               =0;
      virtual void  get(int i, BinaryBuffer& bb, size_t extra = 0)      =0;
      virtual void  destroy(int i)                                      =0;
  };

  class FileStorage: public ExternalStorage
  {
    public:
                    FileStorage(const std::string& filename_template = "/tmp/DIY.XXXXXX"):
                      filename_templates_(1, filename_template),
                      count_(0), current_size_(0), max_size_(0)         {}

                    FileStorage(const std::vector<std::string>& filename_templates):
                      filename_templates_(filename_templates),
                      count_(0), current_size_(0), max_size_(0)         {}

      virtual int   put(BinaryBuffer& bb)
      {
        std::string     filename;
        if (filename_templates_.size() == 1)
            filename = filename_templates_[0].c_str();
        else
        {
            // pick a template at random (very basic load balancing mechanism)
            filename  = filename_templates_[std::rand() % filename_templates_.size()].c_str();
        }
#ifdef __MACH__
        // TODO: figure out how to open with O_SYNC
        int fh = mkstemp(const_cast<char*>(filename.c_str()));
#else
        int fh = mkostemp(const_cast<char*>(filename.c_str()), O_WRONLY | O_SYNC);
#endif

        //fprintf(stdout, "FileStorage::put(): %s; buffer size: %lu\n", filename.c_str(), bb.size());

        size_t sz = bb.buffer.size();
        size_t written = write(fh, &bb.buffer[0], sz);
        if (written < sz || written == -1)
          fprintf(stderr, "Warning: could not write the full buffer to %s: written = %lu; size = %lu\n", filename.c_str(), written, sz);
        fsync(fh);
        close(fh);
        bb.wipe();

#if 0       // double-check the written file size: only for extreme debugging
        FILE* fp = fopen(filename.c_str(), "r");
        fseek(fp, 0L, SEEK_END);
        int fsz = ftell(fp);
        if (fsz != sz)
            fprintf(stderr, "Warning: file size doesn't match the buffer size, %d vs %d\n", fsz, sz);
        fclose(fp);
#endif

        int res = (*count_.access())++;
        FileRecord  fr = { sz, filename };
        (*filenames_.access())[res] = fr;

        // keep track of sizes
        critical_resource<size_t>::accessor     cur = current_size_.access();
        *cur += sz;
        critical_resource<size_t>::accessor     max = max_size_.access();
        if (*cur > *max)
            *max = *cur;

        return res;
      }

      virtual void   get(int i, BinaryBuffer& bb, size_t extra)
      {
        FileRecord      fr;
        {
          CriticalMapAccessor accessor = filenames_.access();
          fr = (*accessor)[i];
          accessor->erase(i);
        }

        //fprintf(stdout, "FileStorage::get(): %s\n", fr.name.c_str());

        bb.buffer.reserve(fr.size + extra);
        bb.buffer.resize(fr.size);
        int fh = open(fr.name.c_str(), O_RDONLY | O_SYNC, 0600);
        read(fh, &bb.buffer[0], fr.size);
        close(fh);

        remove(fr.name.c_str());

        (*current_size_.access()) -= fr.size;
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
        (*current_size_.access()) -= fr.size;
      }

      int           count() const               { return (*count_.const_access()); }
      size_t        current_size() const        { return (*current_size_.const_access()); }
      size_t        max_size() const            { return (*max_size_.const_access()); }

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
      std::vector<std::string>      filename_templates_;
      CriticalMap                   filenames_;
      critical_resource<int>        count_;
      critical_resource<size_t>     current_size_, max_size_;
  };
}

#endif
