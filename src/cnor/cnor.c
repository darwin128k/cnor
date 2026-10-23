#include <cnor/cnor.h>
#include <lh/version/initializer.h>

static const lh_version_t m_version =
    lh_version_initializer(CNOR_VERSION_MAJOR, CNOR_VERSION_MINOR, CNOR_VERSION_PATCH);

const lh_version_t *
cnor_get_version(void)
{
    return lh_addr_of(m_version);
}
