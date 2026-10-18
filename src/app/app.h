#ifndef REVERSI_APP_APP_H
#define REVERSI_APP_APP_H

/*
 * Runs the application until the window is closed.
 *
 * When max_frames is greater than zero the loop stops after that many
 * rendered frames. This exists for automated smoke tests; zero means run
 * normally. Returns 0 on success.
 */
int app_run(int max_frames);

#endif
