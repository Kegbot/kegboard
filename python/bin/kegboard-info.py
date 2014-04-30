#!/usr/bin/env python
#
# Copyright 2014 Mike Wakerly <opensource@hoho.com>
#
# This file is part of the Kegboard package of the Kegbot project.
# For more information on Kegboard or Kegbot, see http://kegbot.org/
#
# Kegboard is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# Kegboard is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Kegboard.  If not, see <http://www.gnu.org/licenses/>.

"""Waits for a Kegboard and prints information about it."""

import sys

from kegbot.util import app
from kegbot.kegboard import kegboard

class KegboardMonitorApp(app.App):

  def _SetupSignals(self):
    pass

  def _MainLoop(self):
    self._logger.debug('Waiting for kegboard ...')
    board = kegboard.get_kegboard()
    if not board:
      print 'No kegboard found.'
      sys.exit(1)
    board.open()
    self._logger.debug('Got kegboard: %s' % board)

    msg = board.wait_for_ping()
    if not msg:
      print 'Gave up pinging kegboard!'
      sys.exit(1)

    print '%s: firmware_version=%s serial_number=%s' % (board, msg.firmware_version,
        msg.serial_number)


if __name__ == '__main__':
  KegboardMonitorApp.BuildAndRun()
