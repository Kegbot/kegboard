.. _install-kegboard-library:

Kegboard Support Library
========================

In addition to the Kegboard firmware, the Kegboard package comes with some
support optional tools (``kegboard-monitor`` and ``kegboard-tester``) as well as
a Python library for writing new programs to talk to Kegboard.  This section
will walk you through installing them.

.. note::
  The support tools are automatically installed as part of Pycore.  If you've
  installed Pycore, you can skip this section.

Set up virtualenv
-----------------

.. note::
  If you've already installed Kegbot Server or Pycore in its own virtualenv, you
  don't need to create a new one just for Kegbot; it's perfectly fine to reuse
  the existing virtualenv.

The ``virtualenv`` tool creates a directory where the Kegboard support programs
and all of their Python dependencies will be stored.  It makes it easier to
install the tools without needing root privileges, and reduces the chance of
Kegboard clashing with your system's Python modules.

The first time you set up Kegboard, you will need to create a new virtualenv
"home" for it.  Any filesystem location is fine.  To create it, give the
directory name as the only argument.  The example below creates the Kegboard
virtualenv directory in your user's home directory::

  $ virtualenv ~/kegboard
  New python executable in /Users/mike/kegboard/bin/python
  Installing setuptools............done.
  Installing pip...............done.

Now that the virtualenv home has been created (at ``~/kegboard/``), there's one
step to remember.  Each time you want to use the virtualenv home (to use the
support tools) you need to activate it for the current shell::

  $ source ~/kegboard/bin/activate
  (kegboard) $

Your shell prompt will be updated with ``(kegboard)`` when the virtualenv is
active.  If you want to step out of the env for some reason, just call
``deactivate``::

  (kegboard) $ deactivate
  $

If you ever want to completely uninstall Kegboard, just delete the entire
``kegboard/`` directory -- there's nothing precious in it, and you can always
recreate it by following these steps again.

.. tip::
  You can install multiple versions of Kegboard simply by creating a new
  virtualenv for each one.


Install Kegboard
----------------

There are two approaches to downloading and installing Kegbot:

* :ref:`From the latest release, using pip <install-kegboard-release>`, the
  recommended way to quickly get going with the latest release.
* :ref:`From Git <install-kegboard-git>`, to grab the latest, bleeding-edge
  development code.  Recommended for advanced users only.

If in doubt, proceed to the next section for the easiest method.  Be sure you
have activated your virtualenv first.


.. _install-kegboard-release:

From Latest Release (Recommended)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use the ``pip`` tool to install the latest release of Kegboard, including its
dependencies::

	(kegboard) $ pip install kegbot-kegboard

The command may take a few minutes as it downloads and installs everything.


.. _install-kegboard-git:

From Git (developers)
^^^^^^^^^^^^^^^^^^^^^

If you want to run Kegboard from the latest development version, follow this
section.

.. warning::
  Running from unreleased git sources is not recommended for production systems,
  since code can be very unstable and functionality may change suddenly.
  **Always** back up your valuable data.  As stated in Kegbot's license, we
  provide Kegbot with absolutely no warranty.

#. Check out the Kegboard sources using ``git``::

	(kegboard) $ git clone https://github.com/Kegbot/kegboard.git

#. Step in to the new tree and run the setup command::

	(kegboard) $ cd kegbot/python
	(kegboard) $ ./setup.py develop

The command may take a few minutes as it downloads and installs everything.

Testing a Kegboard
-------------------

The support library includes two small programs which you can use against a
connected Kegboard.

kegboard-monitor.py
^^^^^^^^^^^^^^^^^^^

This program monitors your system's serial ports for a Kegboard, displaying
information about each one it detects.

kegboard-tester.py
^^^^^^^^^^^^^^^^^^

This program cycles through each relay on the Kegboard (when available),
opening and closing it.
