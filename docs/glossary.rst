:orphan:

Glossary
=========

.. glossary::

   os_window
     alatty has two kinds of windows. Operating System windows, referred to as :term:`OS
     Window <os_window>`, and *alatty windows*. An OS Window consists of one or more alatty
     :term:`tabs <tab>`. Each tab in turn consists of one or more *alatty
     windows* organized in a :term:`layout`.

   tab
     A *tab* refers to a group of :term:`alatty windows <window>`, organized in
     a :term:`layout`. Every :term:`OS Window <os_window>` contains one or more tabs.

   layout
     A *layout* is a system of organizing :term:`alatty windows <window>` in
     groups inside a tab. The layout automatically maintains the size and
     position of the windows, think of a layout as a tiling window manager for
     the terminal. See :doc:`layouts` for details.

   window
     alatty has two kinds of windows. Operating System windows, referred to as :term:`OS
     Window <os_window>`, and *alatty windows*. An OS Window consists of one or more alatty
     :term:`tabs <tab>`. Each tab in turn consists of one or more *alatty
     windows* organized in a :term:`layout`.

   overlay
      An *overlay window* is a :term:`alatty window <window>` that is placed on
      top of an existing alatty window, entirely covering it. Overlays are used
      throughout alatty, for example, to display the :ref:`the scrollback buffer <scrollback>`,
      to display :doc:`hints </kittens/hints>`, for :doc:`unicode input
      </kittens/unicode_input>` etc. Normal overlays are meant for short
      duration popups and so are not considered the :italic:`active window`
      when determining the current working directory or getting input text for
      kittens, launch commands, etc. To create an overlay considered as a
      :italic:`main window` use the :code:`overlay-main` argument to
      :doc:`launch`.

   hyperlinks
      Terminals can have hyperlinks, just like the internet. In alatty you can
      :doc:`control exactly what happens <open_actions>` when clicking on a
      hyperlink, based on the type of link and its URL. See also `Hyperlinks in terminal
      emulators <https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda>`__.

   kittens
      Small, independent statically compiled command line programs that are designed to run
      inside alatty windows and provide it with lots of powerful and flexible
      features such as viewing images, connecting conveniently to remote
      computers, transferring files, inputting unicode characters, etc.

.. _env_vars:

Environment variables
------------------------

Variables that influence alatty behavior
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. envvar:: ALATTY_CONFIG_DIRECTORY

   Controls where alatty looks for :file:`alatty.conf` and other configuration
   files. Defaults to :file:`~/.config/alatty`. For full details of the config
   directory lookup mechanism see, :option:`alatty --config`.

.. envvar:: ALATTY_CACHE_DIRECTORY

   Controls where alatty stores cache files. Defaults to :file:`~/.cache/alatty`
   or :file:`~/Library/Caches/alatty` on macOS.

.. envvar:: ALATTY_RUNTIME_DIRECTORY

   Controls where alatty stores runtime files like sockets. Defaults to
   the :code:`XDG_RUNTIME_DIR` environment variable if that is defined
   otherwise the run directory inside the alatty cache directory is used.

.. envvar:: VISUAL

   The terminal based text editor (such as :program:`vi` or :program:`nano`)
   alatty uses, when, for instance, opening :file:`alatty.conf` in response to
   :sc:`edit_config_file`.

.. envvar:: EDITOR

   Same as :envvar:`VISUAL`. Used if :envvar:`VISUAL` is not set.

.. envvar:: GLFW_IM_MODULE

   Set this to ``ibus`` to enable support for IME under X11.

.. envvar:: ALATTY_WAYLAND_DETECT_MODIFIERS

   When set to a non-empty value, alatty attempts to autodiscover XKB modifiers
   under Wayland. This is useful if using non-standard modifiers like hyper. It
   is possible for the autodiscovery to fail; the default Wayland XKB mappings
   are used in this case. See :pull:`3943` for details.

.. envvar:: SSH_ASKPASS

   Specify the program for SSH to ask for passwords. When this is set, :doc:`ssh
   kitten </kittens/ssh>` will use this environment variable by default. See
   :opt:`askpass <kitten-ssh.askpass>` for details.

.. envvar:: ALATTY_CLONE_SOURCE_CODE

   Set this to some shell code that will be executed in the cloned window with
   :code:`eval` when :ref:`clone-in-alatty <clone_shell>` is used.

.. envvar:: ALATTY_CLONE_SOURCE_PATH

   Set this to the path of a file that will be sourced in the cloned window when
   :ref:`clone-in-alatty <clone_shell>` is used.

.. envvar:: ALATTY_DEVELOP_FROM

   Set this to the directory path of the alatty source code and its Python code
   will be loaded from there. Only works with official binary builds.

.. envvar:: ALATTY_RC_PASSWORD

   Set this to a pass phrase to use the ``kitten @`` remote control command with
   :opt:`remote_control_password`.


Variables that alatty sets when running child programs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. envvar:: LANG

   This is only set on macOS. If the country and language from the macOS user
   settings form an invalid locale, it will be set to :code:`en_US.UTF-8`.

.. envvar:: PATH

   alatty prepends itself to the PATH of its own environment to ensure the
   functions calling :program:`alatty` will work properly.

.. envvar:: ALATTY_WINDOW_ID

   An integer that is the id for the alatty :term:`window` the program is running in.
   Can be used with the :doc:`alatty remote control facility <remote-control>`.

.. envvar:: ALATTY_PID

   An integer that is the process id for the alatty process in which the program
   is running. Allows programs to tell alatty to reload its config by sending it
   the SIGUSR1 signal.

.. envvar:: ALATTY_PUBLIC_KEY

   A public key that programs can use to communicate securely with alatty using
   the remote control protocol. The format is: :code:`protocol:key data`.

.. envvar:: WINDOWID

   The id for the :term:`OS Window <os_window>` the program is running in. Only available
   on platforms that have ids for their windows, such as X11 and macOS.

.. envvar:: TERM

   The name of the terminal, defaults to ``xterm-alatty``. See :opt:`term`.

.. envvar:: TERMINFO

   Path to a directory containing the alatty terminfo database.

.. envvar:: ALATTY_INSTALLATION_DIR

   Path to the alatty installation directory.

.. envvar:: COLORTERM

   Set to the value ``truecolor`` to indicate that alatty supports 16 million
   colors.

.. envvar:: ALATTY_LISTEN_ON

   Set when the :doc:`remote control <remote-control>` facility is enabled and
   the a socket is used for control via :option:`alatty --listen-on` or :opt:`listen_on`.
   Contains the path to the socket. Avoid the need to use :option:`kitten @ --to` when
   issuing remote control commands. Can also be a file descriptor of the form
   fd:num instead of a socket address, in which case, remote control
   communication should proceed over the specified file descriptor.

.. envvar:: ALATTY_PIPE_DATA

   Set to data describing the layout of the screen when running child
   programs using :option:`launch --stdin-source` with the contents of the
   screen/scrollback piped to them.

.. envvar:: ALATTY_CHILD_CMDLINE

   Set to the command line of the child process running in the alatty
   window when calling the notification callback program on terminal bell, see
   :opt:`command_on_bell`.

.. envvar:: ALATTY_COMMON_OPTS

   Set with the values of some common alatty options when running
   kittens, so kittens can use them without needing to load :file:`alatty.conf`.

.. envvar:: ALATTY_SHELL_INTEGRATION

   Set when enabling :ref:`shell_integration`. It is automatically removed by
   the shell integration scripts.

.. envvar:: ZDOTDIR

   Set when enabling :ref:`shell_integration` with :program:`zsh`, allowing
   :program:`zsh` to automatically load the integration script.

.. envvar:: XDG_DATA_DIRS

   Set when enabling :ref:`shell_integration` with :program:`fish`, allowing
   :program:`fish` to automatically load the integration script.

.. envvar:: ENV

   Set when enabling :ref:`shell_integration` with :program:`bash`, allowing
   :program:`bash` to automatically load the integration script.

.. envvar:: ALATTY_OS

   Set when using the include directive in alatty.conf. Can take values:
   ``linux``, ``macos``, ``bsd``.

.. envvar:: ALATTY_HOLD

   Set to ``1`` when alatty is running a shell because of the ``--hold`` flag. Can
   be used to specialize shell behavior in the shell rc files as desired.
