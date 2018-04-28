import sys
import requests

GOOGLE_COOKIE_PREFIX = "download_warning_"
GOOGLE_DRIVE_DOWNLOAD_URL = "https://drive.google.com/uc"
GOOGLE_DRIVE_HDA_ID = "0B1JAKGv3cL1oVExydnFrVUNxV2c"

if "__main__" == __name__:
    if 2 != len(sys.argv):
        raise RuntimeError("Expecting output file argument")

    with requests.Session() as session:
        # Make first request
        initial_request = session.get(GOOGLE_DRIVE_DOWNLOAD_URL,
                                      params=dict(export="download", id=GOOGLE_DRIVE_HDA_ID))
        if 200 != initial_request.status_code:
            raise RuntimeError("Failed to request file on initial request, status code = %s" % initial_request.status_code)

        # Expect html response
        content_type = initial_request.headers["Content-Type"]
        if not content_type.startswith("text/html"):
            raise RuntimeError("Failed to request file on initial request, expecting text/html received %s" % content_type)

        # Find confirm cookie
        cookie = next((value for key, value in session.cookies.items() if key.startswith(GOOGLE_COOKIE_PREFIX)), None)
        if not cookie:
            raise RuntimeError("Failed to find download cookie")

        # Download again
        download_request = session.get(GOOGLE_DRIVE_DOWNLOAD_URL,
                                       params=dict(export="download", id=GOOGLE_DRIVE_HDA_ID, confirm=cookie),
                                       stream=True)

        if 200 != download_request.status_code:
            raise RuntimeError("Failed to request file on second request, status code = %s" % download_request.status_code)

        # Stream into a file
        print "Downloading image to %s" % sys.argv[1]
        with open(sys.argv[1], "wb") as output:
            for chunk in download_request.iter_content(1024 * 1024):
                output.write(chunk)
