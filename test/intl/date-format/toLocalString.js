// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let date = new Date(1532063696649);
assertEquals('7/19/2018, 10:14:56 PM', date.toLocaleString());
assertEquals('7/19/2018', date.toLocaleDateString());
assertEquals('10:14:56 PM', date.toLocaleTimeString());
assertEquals('19/07/2018 à 22:14:56', date.toLocaleString("fr"));
assertEquals('19/07/2018', date.toLocaleDateString("fr"));
assertEquals('22:14:56', date.toLocaleTimeString("fr"));
assertEquals('19.7.2018, 22:14:56', date.toLocaleString("de"));
assertEquals('19.7.2018', date.toLocaleDateString("de"));
assertEquals('22:14:56', date.toLocaleTimeString("de"));
assertEquals('19.7.2018, 22:14:56', date.toLocaleString(["de", "fr"]));
assertEquals('19.7.2018', date.toLocaleDateString(["de", "fr"]));
assertEquals('22:14:56', date.toLocaleTimeString(["de", "fr"]));

var opt1 = {
  weekday: 'long',
};
assertEquals('Thursday', date.toLocaleString([], opt1));
assertEquals('Thursday', date.toLocaleDateString([], opt1));
assertEquals('Thursday 10:14:56 PM', date.toLocaleTimeString([], opt1));
assertEquals('jeudi', date.toLocaleString(["fr"], opt1));
assertEquals('jeudi', date.toLocaleDateString(["fr"], opt1));
assertEquals('jeudi 22:14:56', date.toLocaleTimeString(["fr"], opt1));
assertEquals('Donnerstag', date.toLocaleString(["de", "en", "fr"], opt1));
assertEquals('Donnerstag', date.toLocaleDateString(["de", "en", "fr"], opt1));
assertEquals('Donnerstag, 22:14:56', date.toLocaleTimeString(["de", "en", "fr"], opt1));

var opt2 = {
  day: 'numeric',
  month: 'long',
};
assertEquals('July 19', date.toLocaleString([], opt2));
assertEquals('July 19', date.toLocaleDateString([], opt2));
assertEquals('July 19, 10:14:56 PM', date.toLocaleTimeString([], opt2));
assertEquals('19 juillet', date.toLocaleString(["fr"], opt2));
assertEquals('19 juillet', date.toLocaleDateString(["fr"], opt2));
assertEquals('19 juillet à 22:14:56', date.toLocaleTimeString(["fr"], opt2));
assertEquals('19. Juli', date.toLocaleString(["de", "en", "fr"], opt2));
assertEquals('19. Juli', date.toLocaleDateString(["de", "en", "fr"], opt2));
assertEquals('19. Juli, 22:14:56', date.toLocaleTimeString(["de", "en", "fr"], opt2));

var opt3 = {
  day: 'numeric',
  month: 'long',
  year: '2-digit',
  era: 'long',
};
assertEquals('July 19, 18 Anno Domini', date.toLocaleString([], opt3));
assertEquals('July 19, 18 Anno Domini', date.toLocaleDateString([], opt3));
assertEquals('July 19, 18 Anno Domini, 10:14:56 PM', date.toLocaleTimeString([], opt3));
assertEquals('19 juillet 18 après Jésus-Christ', date.toLocaleString(["fr"], opt3));
assertEquals('19 juillet 18 après Jésus-Christ', date.toLocaleDateString(["fr"], opt3));
assertEquals('19 juillet 18 après Jésus-Christ à 22:14:56', date.toLocaleTimeString(["fr"], opt3));
assertEquals('19. Juli 18 n. Chr.', date.toLocaleString(["de", "en", "fr"], opt3));
assertEquals('19. Juli 18 n. Chr.', date.toLocaleDateString(["de", "en", "fr"], opt3));
assertEquals('19. Juli 18 n. Chr., 22:14:56', date.toLocaleTimeString(["de", "en", "fr"], opt3));

var opt4 = {
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
};
assertEquals('10:14:56 PM', date.toLocaleString([], opt4));
assertEquals('7/19/2018, 10:14:56 PM', date.toLocaleDateString([], opt4));
assertEquals('10:14:56 PM', date.toLocaleTimeString([], opt4));
assertEquals('22:14:56', date.toLocaleString(["fr"], opt4));
assertEquals('19/07/2018 à 22:14:56', date.toLocaleDateString(["fr"], opt4));
assertEquals('22:14:56', date.toLocaleTimeString(["fr"], opt4));
assertEquals('22:14:56', date.toLocaleString(["de", "en", "fr"], opt4));
assertEquals('19.7.2018, 22:14:56', date.toLocaleDateString(["de", "en", "fr"], opt4));
assertEquals('22:14:56', date.toLocaleTimeString(["de", "en", "fr"], opt4));

var opt5 = {
  hour: 'numeric',
  minute: 'numeric',
};
assertEquals('10:14 PM', date.toLocaleString([], opt5));
assertEquals('7/19/2018, 10:14 PM', date.toLocaleDateString([], opt5));
assertEquals('10:14 PM', date.toLocaleTimeString([], opt5));
assertEquals('22:14', date.toLocaleString(["fr"], opt5));
assertEquals('19/07/2018 à 22:14', date.toLocaleDateString(["fr"], opt5));
assertEquals('22:14', date.toLocaleTimeString(["fr"], opt5));
assertEquals('22:14', date.toLocaleString(["de", "en", "fr"], opt5));
assertEquals('19.7.2018, 22:14', date.toLocaleDateString(["de", "en", "fr"], opt5));
assertEquals('22:14', date.toLocaleTimeString(["de", "en", "fr"], opt5));
