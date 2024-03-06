// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"os"
	"os/user"
	"path/filepath"
	"runtime"
	"strings"
	"sync"

	"golang.org/x/sys/unix"
)

var Sep = string(os.PathSeparator)

func Expanduser(path string) string {
	if !strings.HasPrefix(path, "~") {
		return path
	}
	home, err := os.UserHomeDir()
	if err != nil {
		usr, err := user.Current()
		if err == nil {
			home = usr.HomeDir
		}
	}
	if err != nil || home == "" {
		return path
	}
	if path == "~" {
		return home
	}
	path = strings.ReplaceAll(path, Sep, "/")
	parts := strings.Split(path, "/")
	if parts[0] == "~" {
		parts[0] = home
	} else {
		uname := parts[0][1:]
		if uname != "" {
			u, err := user.Lookup(uname)
			if err == nil && u.HomeDir != "" {
				parts[0] = u.HomeDir
			}
		}
	}
	return strings.Join(parts, Sep)
}

func Abspath(path string) string {
	q, err := filepath.Abs(path)
	if err == nil {
		return q
	}
	return path
}

var AlattyExe = sync.OnceValue(func() string {
	exe, err := os.Executable()
	if err == nil {
		ans := filepath.Join(filepath.Dir(exe), "alatty")
		if s, err := os.Stat(ans); err == nil && !s.IsDir() {
			return ans
		}
	}
	return os.Getenv("ALATTY_PATH_TO_ALATTY_EXE")
})

func ConfigDirForName(name string) (config_dir string) {
	if kcd := os.Getenv("ALATTY_CONFIG_DIRECTORY"); kcd != "" {
		return Abspath(Expanduser(kcd))
	}
	var locations []string
	seen := NewSet[string]()
	add := func(x string) {
		x = Abspath(Expanduser(x))
		if !seen.Has(x) {
			seen.Add(x)
			locations = append(locations, x)
		}
	}
	if xh := os.Getenv("XDG_CONFIG_HOME"); xh != "" {
		add(xh)
	}
	if dirs := os.Getenv("XDG_CONFIG_DIRS"); dirs != "" {
		for _, candidate := range strings.Split(dirs, ":") {
			add(candidate)
		}
	}
	add("~/.config")
	if runtime.GOOS == "darwin" {
		add("~/Library/Preferences")
	}
	for _, loc := range locations {
		if loc != "" {
			q := filepath.Join(loc, "alatty")
			if _, err := os.Stat(filepath.Join(q, name)); err == nil {
				if unix.Access(q, unix.W_OK) == nil {
					config_dir = q
					return
				}
			}
		}
	}
	config_dir = os.Getenv("XDG_CONFIG_HOME")
	if config_dir == "" {
		config_dir = "~/.config"
	}
	config_dir = filepath.Join(Expanduser(config_dir), "alatty")
	return
}

var ConfigDir = sync.OnceValue(func() (config_dir string) {
	return ConfigDirForName("alatty.conf")
})

var CacheDir = sync.OnceValue(func() (cache_dir string) {
	candidate := ""
	if edir := os.Getenv("ALATTY_CACHE_DIRECTORY"); edir != "" {
		candidate = Abspath(Expanduser(edir))
	} else if runtime.GOOS == "darwin" {
		candidate = Expanduser("~/Library/Caches/alatty")
	} else {
		candidate = os.Getenv("XDG_CACHE_HOME")
		if candidate == "" {
			candidate = "~/.cache"
		}
		candidate = filepath.Join(Expanduser(candidate), "alatty")
	}
	os.MkdirAll(candidate, 0o755)
	return candidate
})
