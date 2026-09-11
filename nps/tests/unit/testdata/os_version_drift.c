/* An excerpt of ndl/src/resources/utils.c with index 47 renamed to the CAS build, which is the
 * shape a real drift would take. The comparison in integrity_tests.cc has to reject this before it
 * is worth running against the genuine source. */
void ut_read_os_version_index(void) {
	switch (*(unsigned*)(0x10000020)) {
		case 0x1040E4D0: // 5.2.0.771 non-CAS CX II
			ut_os_version_index = 34;
			break;
		case 0x10429ec0: // 6.4.0.74 CAS CX II
			ut_os_version_index = 47;
			break;
		default:
			ut_calc_reboot();
	}
}
