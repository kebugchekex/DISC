enum DISC_Errors{
  DISC_RETURN_SUCCESS,
  DISC_ERROR_GENERIC,
  DISC_ERROR_PERMISSIONS,
  DISC_ERROR_API_ERROR,
  DISC_ERROR_CONNECTION,
  DISC_ERROR_OBJECT_NONEXISTANT,
};

void DISC_PrintError();

void DISC_AddError(int error);