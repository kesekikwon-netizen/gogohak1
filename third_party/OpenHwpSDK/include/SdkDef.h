#pragma once

#ifdef _WIN32
#include <windows.h>
#elif !defined(HWND_DEFINED)
#define HWND_DEFINED
typedef void* HWND;
#endif

#ifndef FAR
#define FAR
#endif

//typedef HWND window_platform_handle;

namespace HWPSDK {

	/**
	* \defgroup hwpsdk_def
	* @{
	*/

	/**
	* @enum ImageMakeOption
	* @brief CreatePageImageEx API에서 사용되는 옵션입니다.
	*/
	typedef enum {
		CI_NULL = 0x00,							/**< option 없음 */
		CI_HIDE_FIELD_DESCRIPTION = 0x01,		/**< 누름틀 안내문을 감춤 */
	} ImageMakeOption;

	/**
	* @enum FieldProperties
	* @brief GetCurFieldState API에서 사용됩니다.
	*/
	typedef enum {
		FIELD_NONE = 0x00, /**< 없음 */
		FIELD_CELL = 0x01, /**< 셀 */
		FIELD_CLICKHERE = 0x02, /**< 누름틀 */
		FIELD_TEXTBOX = 0x04, /**< 글상자 */
		FIELD_NAME_NONE = 0x10 /**< 이름 없음 */
	} FieldProperties;

	/**
	* @enum FieldOption
	* @brief  필드 API에서 사용되는 옵션이며 필드의 종류입니다.
	*/
	typedef enum {
		FieldCell = 0x01,		/**< 셀에 부여된 필드 목록만 가져옵니다. FieldClickHere, FieldTextbox와는 함께 지정할 수 없습니다. */
		FieldClickHere = 0x02,	/**< 누름틀에 부여된 필드 목록만 가져옵니다. FieldCell, FieldTextbox와는 함께 지정할 수 없습니다. */
		FieldSelection = 0x04,	/**< 셀렉션 내에 존재하는 필드 목록만 가져옵니다. */
		FieldTextbox = 0x08		/**< 글상자에 부여된 필드 목록만 가져옵니다. FieldCell, FieldClickHere와는 함께 지정할 수 없습니다. */
	} FieldOption;

	/**
	* @enum FieldNameNumber
	* @brief  필드 API에서 사용되는 옵션이며 필드의 종류입니다.
	*/
	typedef enum {
		FieldPlain = 0,			/**< 아무 기호 없이 순서 대로 필드 이름이 나열됩니다. */
		FieldNumber = 1,		/**< 필드 이름 뒤에 일련 번호가 {{#}}와 같은 형식으로 추가됩니다. */
		FieldCount = 2,			/**< 필드 이름 뒤에 필드의 개수가 {{#}}와 같은 형식으로 추가됩니다. */
	} FieldNameNumber;

	/**
	* @enum GetTextOption
	* @brief
	*/
	typedef enum {
		HWPGETTEXTRET_FAIL = 0,				/**< 텍스트 정보가 없습니다. */
		HWPGETTEXTRET_ENDOFLIST,			/**< 목록의 끝입니다. */
		HWPGETTEXTRET_CONTINUE,				/**< 일반 텍스트입니다. */
		HWPGETTEXTRET_NEXT_PARA,			/**< 다음 문단입니다. */
		HWPGETTEXTRET_ENTER_CTRL,			/**< 제어 문자 내부로 들어갑니다. */
		HWPGETTEXTRET_EXIT_CTRL,			/**< 제어 문자를 빠져 나옵니다. */
		HWPGETTEXTRET_ERROR_INITSCAN = 101,	/**< IHwpScan interface 초기화가 실행되지 않습니다. */
		HWPGETTEXTRET_ERROR_CONVERSION,		/**< 텍스트 변환에 실패했습니다. */
	}GetTextOption;


	/**
	* @enum MetatagOption
	* @brief
	*/
	typedef enum {
		MetatagSelection = 0x10000,		/**< 선택 영역입니다. */
		MetatagPara = 0x01,				/**< 문단입니다. */
		MetatagCell = 0x02,				/**< 셀입니다. */
		MetatagTable = 0x04,			/**< 표입니다. */
		MetatagClickHere = 0x08,		/**< 누름틀입니다. */
		MetatagShapeObject = 0x10,		/**< 이미지 개체입니다. */
		MetatagPicture = 0x20,			/**< 이미지입니다. */
	} MetatagOption;

	/**
	* @enum MetatagNameNumber
	* @brief
	*/
	typedef enum {
		MetatagPlain = 0x00,		/**<  ?? */
		MetatagNumber = 0x01,		/**<  ?? */
		MetatagCount = 0x02			/**<  ?? */
	} MetatagNameNumber;

	/**
	* @enum MetatagProperties
	* @brief
	*/
	typedef enum {
		METATAG_NONE = 0x00,		/**<    */
		METATAG_CELL = 0x01,		/**<    */
		METATAG_CLICKHERE = 0x02,	/**<    */
		METATAG_TEXTBOX = 0x04,		/**<    */
		METATAG_SHAPE = 0x08,		/**<    */
		METATAG_TABLE = 0x10,		/**<    */
		METATAG_NAME_NONE = 0x20	/**<    */
	} MetatagProperties;

	/**
	* @enum MoveID
	* @brief
	*/
	typedef enum {
		moveMain = 0,			/**< 루트 목록의 특정 위치(para, pos로 위치 지정) */
		moveCurList,			/**< 현재 목록의 특정 위치(para, pos로 위치 지정) */

		moveTopOfFile,			/**< 문서의 시작 지점으로 이동 */
		moveBottomOfFile,		/**< 문서의 끝 지점으로 이동 */
		moveTopOfList,			/**< 현재 목록의 시작 지점으로 이동 */
		moveBottomOfList,		/**< 현재 목록의 끝 지점으로 이동 */
		moveStartOfPara,		/**< 현재 위치한 문단의 시작 지점으로 이동 */
		moveEndOfPara,			/**< 현재 위치한 문단의 끝 지점으로 이동 */
		moveStartOfWord,		/**< 현재 위치한 단어의 시작 지점으로 이동(현재 목록만 대상으로 동작)  */
		moveEndOfWord,			/**< 현재 위치한 단어의 끝 지점으로 이동(현재 목록만 대상으로 동작) */
		moveNextPara,			/**< 다음 문단의 시작 지점으로 이동(현재 목록만 대상으로 동작) */
		movePrevPara,			/**< 앞 문단의 끝 지점으로 이동(현재 목록만 대상으로 동작) */
		moveNextPos,			/**< 한 글자 앞으로 이동(하위 목록으로 이동 가능) */
		movePrevPos,			/**< 한 글자 뒤로 이동(하위 목록으로 이동 가능) */
		moveNextPosEx,			/**< 한 글자 앞으로 이동(하위 목록으로 이동 가능하며 머리말/꼬리말, 각주/미주, 글상자 포함) */
		movePrevPosEx,			/**< 한 글자 뒤로 이동(하위 목록으로 이동 가능하며 머리말/꼬리말, 각주/미주, 글상자 포함) */
		moveNextChar,			/**< 한 글자 앞으로 이동(현재 목록만 대상으로 동작) */
		movePrevChar,			/**< 한 글자 뒤로 이동(현재 목록만 대상으로 동작) */
		moveNextWord,			/**< 한 단어 앞으로 이동(현재 목록만 대상으로 동작) */
		movePrevWord,			/**< 한 단어 뒤로 이동(현재 목록만 대상으로 동작) */
		moveNextLine,			/**< 한 줄 위로 이동 */
		movePrevLine,			/**< 한 줄 아래로 이동 */
		moveStartOfLine,		/**< 현재 위치한 줄의 시작 지점으로 이동 */
		moveEndOfLine,			/**< 현재 위치한 줄의 끝 지점으로 이동 */

		moveParentList,			/**< 한 수준 위로 이동 */
		moveTopLevelList,		/**< 최상위 목록으로 이동 */
		moveRootList,			/**< 루트 목록으로 이동
								 추가 설명:
								 현재 루트 목록에 위치해 있어 더 이상 상위 목록이 없을
								 경우 위치 이동 없이 반환합니다. 이동한 후의 위치는 상위 목록에서
								 하위 목록이 속한 컨트롤 코드가 위치한 곳입니다.
								 위치 이동 시 셀렉션은 해제됩니다.*/
		moveCurrentCaret,		/**< 현재 캐럿 위치로 이동. 캐럿 위치가 뷰의 맨 위로 올라감 */

		moveLeftOfCell = 100,	/**< 현재 캐럿이 위치한 셀의 왼쪽 */
		moveRightOfCell,		/**< 현재 캐럿이 위치한 셀의 오른쪽 */
		moveUpOfCell,			/**< 현재 캐럿이 위치한 셀의 위쪽 */
		moveDownOfCell,			/**< 현재 캐럿이 위치한 셀의 아래쪽 */
		moveStartOfCell,		/**< 현재 캐럿이 위치한 셀에서 행(row)의 시작 */
		moveEndOfCell,			/**< 현재 캐럿이 위치한 셀에서 행(row)의 끝 */
		moveTopOfCell,			/**< 현재 캐럿이 위치한 셀에서 열(column)의 시작 */
		moveBottomOfCell,		/**< 현재 캐럿이 위치한 셀에서 열(column)의 끝 */

		moveScrPos = 200,		/**< 한/글 문서 창에서 화면 좌표로서 위치를 설정 */
		moveScanPos,			/**< GetText() 실행 후 위치로 이동 */
	} MoveID;

	/**
	* @enum SaveType
	* @brief
	*/
	typedef enum {
		SAVEEX_SAVEAS,			/**< 다른 이름으로 저장 */
		SAVEEX_SAVE,			/**< 저장 */
		SAVEEX_SAVEBLOCK,		/**< 블록 저장 */
	} SaveType;

	/**
	* @enum DiffReportType
	* @brief 비교 결과 파일 생성 옵션입니다.
	*/
	typedef enum {
		DOCDIFF_REPORT_TYPE_BOOKMARK = 0x0001, /**< 책갈피로 결과 파일 생성 */
		DOCDIFF_REPORT_TYPE_MEMO = 0x0002, /**< 메모로 결과 파일 생성 */
		DOCDIFF_REPORT_TYPE_REVISION = 0x0004, /**< 교정 부호로 결과 파일 생성 */
	} DiffReportType;

	/**
	* @}
	*/
}