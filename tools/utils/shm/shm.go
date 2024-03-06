// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package shm

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"strings"

	"alatty/tools/cli"

	"golang.org/x/sys/unix"
)

var _ = fmt.Print
var ErrPatternHasSeparator = errors.New("The specified pattern has file path separators in it")
var ErrPatternTooLong = errors.New("The specified pattern for the SHM name is too long")

type ErrNotSupported struct {
	err error
}

func (self *ErrNotSupported) Error() string {
	return fmt.Sprintf("POSIX shared memory not supported on this platform: with underlying error: %v", self.err)
}

// prefix_and_suffix splits pattern by the last wildcard "*", if applicable,
// returning prefix as the part before "*" and suffix as the part after "*".
func prefix_and_suffix(pattern string) (prefix, suffix string, err error) {
	for i := 0; i < len(pattern); i++ {
		if os.IsPathSeparator(pattern[i]) {
			return "", "", ErrPatternHasSeparator
		}
	}
	if pos := strings.LastIndexByte(pattern, '*'); pos != -1 {
		prefix, suffix = pattern[:pos], pattern[pos+1:]
	} else {
		prefix = pattern
	}
	return prefix, suffix, nil
}

type MMap interface {
	Close() error
	Unlink() error
	Slice() []byte
	Name() string
	IsFileSystemBacked() bool
	FileSystemName() string
	Stat() (fs.FileInfo, error)
	Flush() error
	Seek(offset int64, whence int) (ret int64, err error)
	Read(b []byte) (n int, err error)
	Write(b []byte) (n int, err error)
}

type AccessFlags int

const (
	READ AccessFlags = iota
	WRITE
	COPY
)

func mmap(sz int, access AccessFlags, fd int, off int64) ([]byte, error) {
	flags := unix.MAP_SHARED
	prot := unix.PROT_READ
	switch access {
	case COPY:
		prot |= unix.PROT_WRITE
		flags = unix.MAP_PRIVATE
	case WRITE:
		prot |= unix.PROT_WRITE
	}

	b, err := unix.Mmap(fd, off, sz, prot, flags)
	if err != nil {
		return nil, err
	}
	return b, nil
}

func munmap(s []byte) error {
	return unix.Munmap(s)
}

func truncate_or_unlink(ans *os.File, size uint64, unlink func(string) error) (err error) {
	fd := int(ans.Fd())
	sz := int64(size)
	if err = Fallocate_simple(fd, sz); err != nil {
		if !errors.Is(err, errors.ErrUnsupported) {
			return fmt.Errorf("fallocate() failed on fd from shm_open(%s) with size: %d with error: %w", ans.Name(), size, err)
		}
		for {
			if err = unix.Ftruncate(fd, sz); !errors.Is(err, unix.EINTR) {
				break
			}
		}
	}
	if err != nil {
		_ = ans.Close()
		_ = unlink(ans.Name())
		return fmt.Errorf("Failed to ftruncate() SHM file %s to size: %d with error: %w", ans.Name(), size, err)
	}
	return
}

func Write(self MMap, b []byte) (n int, err error) {
	if len(b) == 0 {
		return 0, nil
	}
	pos, _ := self.Seek(0, io.SeekCurrent)
	if pos < 0 {
		pos = 0
	}
	s := self.Slice()
	if pos >= int64(len(s)) {
		return 0, io.ErrShortWrite
	}
	n = copy(s[pos:], b)
	if _, err = self.Seek(int64(n), io.SeekCurrent); err != nil {
		return n, err
	}
	if n < len(b) {
		return n, io.ErrShortWrite
	}
	return n, nil
}
