// License: GPLv3 Copyright: 2023, Kovid Goyal, <kovid at kovidgoyal.net>

package tui

import (
	"fmt"
	"alatty"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"

	"github.com/shirou/gopsutil/v3/process"
	"golang.org/x/sys/unix"

	"alatty/tools/config"
	"alatty/tools/tty"
	"alatty/tools/tui/loop"
	"alatty/tools/utils"
	"alatty/tools/utils/shlex"
)

var _ = fmt.Print

type AlattyOpts struct {
	Shell, Shell_integration string
}

func read_relevant_alatty_opts(path string) AlattyOpts {
	ans := AlattyOpts{Shell: alatty.AlattyConfigDefaults.Shell, Shell_integration: alatty.AlattyConfigDefaults.Shell_integration}
	handle_line := func(key, val string) error {
		switch key {
		case "shell":
			ans.Shell = strings.TrimSpace(val)
		}
		return nil
	}
	cp := config.ConfigParser{LineHandler: handle_line}
	_ = cp.ParseFiles(path)
	if ans.Shell == "" {
		ans.Shell = alatty.AlattyConfigDefaults.Shell
	}
	return ans
}

func get_effective_ksi_env_var(x string) string {
	parts := strings.Split(strings.TrimSpace(strings.ToLower(x)), " ")
	current := utils.NewSetWithItems(parts...)
	if current.Has("disabled") {
		return ""
	}
	allowed := utils.NewSetWithItems(alatty.AllowedShellIntegrationValues...)
	if !current.IsSubsetOf(allowed) {
		return relevant_alatty_opts().Shell_integration
	}
	return x
}

var relevant_alatty_opts = sync.OnceValue(func() AlattyOpts {
	return read_relevant_alatty_opts(filepath.Join(utils.ConfigDir(), "alatty.conf"))
})

func get_shell_from_alatty_conf() (shell string) {
	shell = relevant_alatty_opts().Shell
	if shell == "." {
		s, e := utils.LoginShellForCurrentUser()
		if e != nil {
			shell = "/bin/sh"
		} else {
			shell = s
		}
	}
	return
}

func find_shell_parent_process() string {
	var p *process.Process
	var err error
	for {
		if p == nil {
			p, err = process.NewProcess(int32(os.Getppid()))
		} else {
			p, err = p.Parent()
		}
		if err != nil {
			return ""
		}
	}
}

func ResolveShell(shell string) []string {
	switch shell {
	case "":
		shell = get_shell_from_alatty_conf()
	case ".":
		if shell = find_shell_parent_process(); shell == "" {
			shell = get_shell_from_alatty_conf()
		}
	}
	shell_cmd, err := shlex.Split(shell)
	if err != nil {
		shell_cmd = []string{shell}
	}
	exe := utils.FindExe(shell_cmd[0])
	if unix.Access(exe, unix.X_OK) != nil {
		shell_cmd = []string{"/bin/sh"}
	}
	return shell_cmd
}

func ResolveShellIntegration(shell_integration string) string {
	if shell_integration == "" {
		shell_integration = relevant_alatty_opts().Shell_integration
	}
	return get_effective_ksi_env_var(shell_integration)
}

func rc_modification_allowed(ksi string) bool {
	for _, x := range strings.Split(ksi, " ") {
		switch x {
		case "disabled", "no-rc":
			return false
		}
	}
	return ksi != ""
}

func RunShell(shell_cmd []string, cwd string) (err error) {
	var shell_env map[string]string
	exe := shell_cmd[0]
	if runtime.GOOS == "darwin" {
		// ensure shell runs in login mode. On macOS lots of people use ~/.bash_profile instead of ~/.bashrc
		// which means they expect the shell to run in login mode always. Le Sigh.
		shell_cmd[0] = "-" + filepath.Base(shell_cmd[0])
	}
	var env []string
	if shell_env != nil {
		env = make([]string, 0, len(shell_env))
		for k, v := range shell_env {
			env = append(env, fmt.Sprintf("%s=%s", k, v))
		}
	} else {
		env = os.Environ()
	}
	// fmt.Println(fmt.Sprintf("%s %v\n%#v", utils.FindExe(exe), shell_cmd, env))
	if cwd != "" {
		_ = os.Chdir(cwd)
	}
	return unix.Exec(utils.FindExe(exe), shell_cmd, env)
}

func RunCommandRestoringTerminalToSaneStateAfter(cmd []string) {
	exe := utils.FindExe(cmd[0])
	c := exec.Command(exe, cmd[1:]...)
	c.Stdout = os.Stdout
	c.Stdin = os.Stdin
	c.Stderr = os.Stderr
	term, err := tty.OpenControllingTerm()
	if err == nil {
		var state_before unix.Termios
		if term.Tcgetattr(&state_before) == nil {
			if _, err = term.WriteString(loop.SAVE_PRIVATE_MODE_VALUES); err != nil {
				fmt.Fprintln(os.Stderr, "failed to write to controlling terminal with error:", err)
				return
			}
			defer func() {
				_, _ = term.WriteString(strings.Join([]string{
					loop.RESTORE_PRIVATE_MODE_VALUES,
					"\x1b[=u",                      // reset alatty keyboard protocol to legacy
					"\x1b[1 q",                     // blinking block cursor
					loop.DECTCEM.EscapeCodeToSet(), // cursor visible
					"\x1b]112\a",                   // reset cursor color
				}, ""))
				_ = term.Tcsetattr(tty.TCSANOW, &state_before)
				term.Close()
			}()
		} else {
			defer term.Close()
		}
	}
	err = c.Run()
	if err != nil {
		fmt.Fprintln(os.Stderr, cmd[0], "failed with error:", err)
	}
}
