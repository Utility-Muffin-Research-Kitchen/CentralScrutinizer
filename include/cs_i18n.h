#ifndef CS_I18N_H
#define CS_I18N_H

const char *cs_i18n(const char *english);
const char *cs_i18n_language(void);
void cs_i18n_init(const char *res_dir);

#define CS_T(s) cs_i18n(s)

#endif
